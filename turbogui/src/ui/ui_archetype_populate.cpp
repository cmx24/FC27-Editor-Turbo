// FC 27 LE Turbo GUI - 2.20 R3: fill the whole player from a player type, a wanted overall and a tier (see ui_archetype_populate.h).
#include "ui_archetype_populate.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include "app.h"
#include "core/archetype_gen.h"
#include "core/archetypes.h"
#include "core/field_labels.h"
#include "core/overall.h"
#include "imgui.h"
#include "playstyles.h"
#include "ui_archetype.h"

namespace turbo {

namespace ag = archetype_gen;

namespace {

// trait2 / icontrait2 bits the generator manages: the six goalkeeper PlayStyles and the traits Solid Player, Team Player, One Club
// Player, Leadership, Super Sub. The other bits (the two CPU AI behaviours, Injury Prone, unnamed ones) stay as the player has them.
constexpr int64_t kManaged2 = 0x3F | (int64_t(1) << 8) | (int64_t(1) << 9) | (int64_t(1) << 10) | (int64_t(1) << 12) | (int64_t(1) << 13);

struct State {
    PopulateUiState ui;
    ag::Table table;
    double next_try = 0.0;
    int64_t playerid = 0;       // the player the controls were set up for
    int64_t pend_playerid = 0;
    std::string pend_name;
    overall::Attrs pend_before{};
    bool pend_in_career = false;
    bool pend_squad_role = false;       // the squad role will be sent (his club is yours)
    int pend_age = -1;
    int pend_old_potential = 0;
    create_form::Form saved;            // Create: the form before Populate (Undo puts it back)
    int64_t saved_template = 0;
    bool have_saved = false;
    bool open_popup = false;
};

State& st() {
    static State s;
    return s;
}

void ensure_table(App& app, State& s) {
    if (s.ui.table_ok) return;
    const double now = ImGui::GetTime();
    if (now < s.next_try) return;
    s.next_try = now + 2.0;
    std::string err;
    if (ag::load_table(app.bridge.root(), s.table, err)) {
        s.ui.table_ok = true;
        s.ui.table_error.clear();
    } else {
        s.ui.table_error = err;
    }
}

bool read_attrs(App& app, const Table& t, uint64_t rec, overall::Attrs& a) {
    for (int i = 0; i < overall::kAttrCount; ++i) {
        if (!t.has(overall::kAttrNames[i])) return false;
        a[static_cast<size_t>(i)] = static_cast<int>(app.db.get_int(t, rec, overall::kAttrNames[i], 0));
    }
    return true;
}

// ---- words for the review window
std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s.empty() ? "none" : s;
}

std::string position_words(const std::vector<int>& pos) {
    std::vector<std::string> v;
    for (int p : pos) {
        const char* n = labels::position_code_name(p);
        if (p >= 0 && n) v.push_back(n);
    }
    return join(v);
}

std::string role_words(const std::vector<int>& ids) {
    std::vector<std::string> v;
    const labels::NamedCodes* t = labels::named_codes("role1");
    for (int id : ids) {
        if (id <= 0) continue;
        const char* n = t ? labels::find_name(*t, id) : nullptr;
        v.push_back(n ? n : "role " + std::to_string(id));
    }
    return join(v);
}

std::vector<std::string> bit_words(int64_t v, const std::vector<const char*>& names, int lo, int hi) {
    std::vector<std::string> out;
    for (int b = lo; b < hi; ++b)
        if ((v >> b) & 1) out.push_back(b < static_cast<int>(names.size()) ? names[static_cast<size_t>(b)] : "Bit " + std::to_string(b));
    return out;
}

// ---- the changes a spec makes against any source of "what he has now"
using Getter = std::function<int64_t(const std::string&)>;

std::vector<PopulateChange> changes_against(const Table& t, const Getter& get, const ag::PlayerSpec& s, bool in_career) {
    std::vector<PopulateChange> out;
    auto want = [&](const std::string& field, int64_t to) {
        const Field* f = t.field(field);
        if (!f || f->type != FieldType::Int) return;
        const int64_t was = get(field);
        if (was != to) out.push_back({field, was, to});
    };
    for (size_t i = 0; i < 7; ++i) want("preferredposition" + std::to_string(i + 1), i < s.positions.size() ? s.positions[i] : -1);
    for (int i = 0; i < overall::kAttrCount; ++i) want(overall::kAttrNames[i], s.attrs[static_cast<size_t>(i)]);
    for (size_t i = 0; i < 9; ++i) want("role" + std::to_string(i + 1), i < s.roles.size() ? s.roles[i] : 0);
    want("trait1", s.trait1);
    want("icontrait1", s.icontrait1);
    want("trait2", (get("trait2") & ~kManaged2) | s.trait2);
    want("icontrait2", (get("icontrait2") & ~kManaged2) | s.icontrait2);
    want("overallrating", s.overall);
    want("potential", s.potential);
    if (in_career && s.contract_until > 0) {
        want("contractvaliduntil", s.contract_until);
        want("wage", s.wage);
    }
    return out;
}

}  // namespace

std::vector<PopulateChange> populate_changes(App& app, const Table& t, const PlayerRow& p, const ag::PlayerSpec& s, bool in_career) {
    return changes_against(t, [&](const std::string& f) { return app.db.get_int(t, p.rec, f, 0); }, s, in_career);
}

int populate_write(App& app, const Table& t, const PlayerRow& p, const std::vector<PopulateChange>& changes, const std::string& label, bool* readback_ok) {
    App::BulkUndo u;
    u.playerid = p.playerid;
    u.label = label;
    int n = 0;
    bool failed = false;
    for (const PopulateChange& c : changes) {
        const Field* f = t.field(c.field);
        if (!f || !app.edit_field(t, p.rec, *f, Value::of_int(c.to))) {
            failed = true;
            break;
        }
        u.before.push_back({c.field, c.was});
        ++n;
    }
    if (failed) {
        for (auto it = u.before.rbegin(); it != u.before.rend(); ++it)
            if (const Field* f = t.field(it->first)) app.edit_field(t, p.rec, *f, Value::of_int(it->second), false);
        if (readback_ok) *readback_ok = false;
        return -1;
    }
    app.push_bulk_undo(std::move(u));
    if (readback_ok) {
        bool ok = true;
        for (const PopulateChange& c : changes) ok = ok && app.db.get_int(t, p.rec, c.field, c.was) == c.to;
        *readback_ok = ok;
    }
    return n;
}

bool populate_undo(App& app, const Table& t, const PlayerRow& p) {
    auto& stack = app.bulk_undo;
    for (size_t i = stack.size(); i-- > 0;) {
        if (stack[i].playerid != p.playerid) continue;
        App::BulkUndo u = stack[i];
        stack.erase(stack.begin() + static_cast<long>(i));
        for (const auto& b : u.before)
            if (const Field* f = t.field(b.first)) app.edit_field(t, p.rec, *f, Value::of_int(b.second));
        app.notify("undone: " + u.label);
        return true;
    }
    return false;
}

const PopulateUiState& populate_ui_state() { return st().ui; }

namespace {

// ---- the three controls: shared by the Edit page and the Create dialog
struct Controls {
    bool changed_arch = false;
};

void draw_controls(State& s, const std::string& pf, int position_for_order, int default_target, bool side_matters_default_left) {
    PopulateUiState& u = s.ui;
    const std::vector<archetypes::Archetype>& all = archetypes::all();
    if (u.arch >= static_cast<int>(all.size())) u.arch = -1;
    if (!u.target_typed) u.target = std::max(1, std::min(99, default_target));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Player type");
    ImGui::SameLine();
    archetype_combo(("##" + pf + "arch").c_str(), u.arch, position_for_order, S(220.0f));
    ImGui::SameLine();
    ImGui::TextUnformatted("Tier");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(120.0f));
    if (ImGui::BeginCombo(("##" + pf + "tier").c_str(), ag::tier_name(static_cast<ag::Tier>(u.tier)))) {
        for (int i = 0; i < ag::kTierCount; ++i) {
            if (ImGui::Selectable(ag::tier_name(static_cast<ag::Tier>(i)), u.tier == i)) u.tier = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ag::tier_blurb(static_cast<ag::Tier>(i)));
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nThe tier sets the squad role, the number of PlayStyles and roles, the potential, the contract and the wage.",
                          ag::tier_blurb(static_cast<ag::Tier>(u.tier)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Wanted overall");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(70.0f));
    int tv = u.target;
    if (ImGui::InputInt(("##" + pf + "target").c_str(), &tv, 0)) {
        u.target = std::max(1, std::min(99, tv));
        u.target_typed = true;
    }
    // right or left: only for the types that have a side (full-backs, wide midfielders, wingers)
    bool sided = false;
    if (u.table_ok && u.arch >= 0)
        if (const ag::Row* r = s.table.find(all[static_cast<size_t>(u.arch)].name))
            for (int p : r->positions) sided = sided || ag::mirror_position(p) != p;
    if (sided) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Side");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        const char* sides[] = {"Right", "Left"};
        ImGui::Combo(("##" + pf + "side").c_str(), &u.side, sides, 2);
    }
    (void)side_matters_default_left;
}

// the review window: what changes, line by line
void row(const char* label, const std::string& now, const std::string& to) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(S(120.0f));
    ImGui::TextDisabled("%s", now.c_str());
    ImGui::SameLine(S(300.0f));
    if (now == to) ImGui::TextDisabled("%s", to.c_str());
    else ImGui::Text("-> %s", to.c_str());
}

std::string num(int64_t v) { return std::to_string(v); }

// The request of the current controls
ag::Request make_request(App& app, const State& s, const overall::Attrs& base, bool in_career) {
    ag::Request rq;
    rq.archetype = archetypes::all()[static_cast<size_t>(s.ui.arch)].name;
    rq.ovr_target = s.ui.target;
    rq.tier = static_cast<ag::Tier>(s.ui.tier);
    rq.side = s.ui.side == 1 ? ag::Side::Left : ag::Side::Right;
    rq.base = base;
    rq.today_year = in_career ? app.today().year : 0;
    return rq;
}

bool left_position(int pos) { return pos == 7 || pos == 16 || pos == 27 || pos == 8 || pos == 19 || pos == 22 || pos == 26 || pos == 11 || pos == 6 || pos == 15; }

void popup_header(const State& s, const std::string& who) {
    const PopulateUiState& u = s.ui;
    ImGui::Text("%s as %s, wanted overall %d, %s", who.c_str(), archetypes::all()[static_cast<size_t>(u.arch)].name, u.target,
                ag::tier_name(static_cast<ag::Tier>(u.tier)));
    ImGui::TextDisabled("%s", ag::tier_blurb(static_cast<ag::Tier>(u.tier)));
}

}  // namespace

// ================================================================================================================ Edit player

void archetype_populate_panel(App& app, const Table& t, const PlayerRow& p) {
    State& s = st();
    PopulateUiState& u = s.ui;
    u.create_mode = false;
    ensure_table(app, s);
    ImGui::SeparatorText("Fill the whole player from a player type");
    if (!u.table_ok) {
        ImGui::TextDisabled("The player type list is not available: %s", u.table_error.c_str());
        return;
    }
    overall::Attrs cur{};
    if (!read_attrs(app, t, p.rec, cur)) {
        ImGui::TextDisabled("Needs all 34 attribute fields; this database lacks some.");
        return;
    }
    if (s.playerid != p.playerid) {
        s.playerid = p.playerid;
        u.target_typed = false;
        u.side = left_position(p.position) ? 1 : 0;
    }
    draw_controls(s, "p", p.position, static_cast<int>(app.db.get_int(t, p.rec, "overallrating", p.overall)), false);
    ImGui::SameLine();
    const bool can = u.arch >= 0;
    if (!can) ImGui::BeginDisabled();
    const BridgeState& bs = app.bridge.state();
    if (ImGui::Button("Review changes...###popreview") && can) {
        s.pend_playerid = p.playerid;
        s.pend_name = p.name;
        s.pend_before = cur;
        s.pend_in_career = bs.in_cm;
        s.pend_squad_role = bs.in_cm && bs.user_team > 0 && p.club == bs.user_team;
        s.pend_age = p.age;
        const ag::Request rq = make_request(app, s, cur, bs.in_cm);
        u.pend_ok = ag::populate(s.table, rq, u.pend);
        u.pend_changes = u.pend_ok ? populate_changes(app, t, p, u.pend, bs.in_cm) : std::vector<PopulateChange>();
        ImGui::OpenPopup("Fill the whole player");
    }
    if (!can) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Shows every position, attribute, role, PlayStyle, trait, potential and squad role that Populate would set.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("One player type, one overall and one tier set his positions, attributes, roles, PlayStyles, traits, potential and squad role.");
    ImGui::PopTextWrapPos();

    // the last Populate: one click puts it back while it is his newest big change
    if (u.applied_playerid == p.playerid && !u.applied_label.empty()) {
        const App::BulkUndo* last = nullptr;
        for (const auto& b : app.bulk_undo)
            if (b.playerid == p.playerid) last = &b;
        if (last && last->label == u.applied_label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Populated: %s.", u.applied_label.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Undo populate###popundo")) populate_undo(app, t, p);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", u.applied_note.c_str());
        }
    }

    if (ImGui::BeginPopupModal("Fill the whole player", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (s.pend_playerid != p.playerid || u.arch < 0) {
            ImGui::TextDisabled("Another player is selected.");
            if (ImGui::Button("Close##pop")) ImGui::CloseCurrentPopup();
        } else {
            const ag::PlayerSpec& sp = u.pend;
            popup_header(s, p.name);
            if (!u.pend_ok) {
                ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "Cannot fill him: %s", sp.why.c_str());
                if (sp.closest > 0) {
                    char ub[48];
                    std::snprintf(ub, sizeof(ub), "Use %d##popclosest", sp.closest);
                    if (ImGui::Button(ub)) {
                        u.target = sp.closest;
                        u.target_typed = true;
                        const ag::Request rq = make_request(app, s, s.pend_before, s.pend_in_career);
                        u.pend_ok = ag::populate(s.table, rq, u.pend);
                        u.pend_changes = u.pend_ok ? populate_changes(app, t, p, u.pend, s.pend_in_career) : std::vector<PopulateChange>();
                    }
                }
            } else {
                for (const std::string& w : sp.warnings) ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", w.c_str());
                if (sp.max_age > 0 && s.pend_age > sp.max_age)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "He is %d: a prospect is %d at most (his age is not changed here).", s.pend_age, sp.max_age);
                // now -> new, line by line
                auto old_int = [&](const char* f) { return app.db.get_int(t, p.rec, f, 0); };
                std::vector<int> old_pos, old_roles;
                for (int i = 1; i <= 7; ++i) old_pos.push_back(static_cast<int>(app.db.get_int(t, p.rec, "preferredposition" + std::to_string(i), -1)));
                for (int i = 1; i <= 9; ++i) old_roles.push_back(static_cast<int>(app.db.get_int(t, p.rec, "role" + std::to_string(i), 0)));
                const bool keeper_new = sp.positions[0] == 0;
                (void)keeper_new;
                row("Positions", position_words(old_pos), position_words(sp.positions));
                row("Roles", role_words(old_roles), role_words(sp.roles));
                auto words = [&](int64_t t1, int64_t t2, bool first) {
                    std::vector<std::string> v = bit_words(t1, playstyle1_names(), 0, 30);
                    if (first) {
                        const std::vector<std::string> g = bit_words(t2, playstyle2_names(), 0, 6);
                        v.insert(v.end(), g.begin(), g.end());
                    }
                    return v;
                };
                row("PlayStyles", join(words(old_int("trait1"), old_int("trait2"), true)), join(words(sp.trait1, sp.trait2, true)));
                row("PlayStyles+", join(words(old_int("icontrait1"), old_int("icontrait2"), true)), join(words(sp.icontrait1, sp.icontrait2, true)));
                {
                    const int64_t keep1 = old_int("trait2") & ~kManaged2, keep2 = old_int("icontrait2") & ~kManaged2;
                    (void)keep1;
                    (void)keep2;
                    row("Traits", join(bit_words(old_int("trait2"), playstyle2_names(), 6, 17)),
                        join(bit_words((old_int("trait2") & ~kManaged2) | sp.trait2, playstyle2_names(), 6, 17)));
                }
                row("Overall", num(old_int("overallrating")), num(sp.overall));
                row("Potential", num(old_int("potential")), num(sp.potential));
                if (s.pend_in_career && sp.contract_until > 0) {
                    row("Contract until", num(old_int("contractvaliduntil")), num(sp.contract_until));
                    row("Wage", num(old_int("wage")), num(sp.wage));
                }
                if (s.pend_squad_role) {
                    static const char* names[] = {"", "Crucial", "Important", "Rotation", "Sporadic", "Prospect"};
                    ImGui::TextUnformatted("Squad role");
                    ImGui::SameLine(S(120.0f));
                    ImGui::Text("-> %s (sent to the game; Undo does not change it)", names[sp.squad_role]);
                } else {
                    ImGui::TextDisabled("Squad role %d (%s) is not written: only players of your club have one.", sp.squad_role, ag::tier_name(sp.tier));
                }
                int n_attr = 0;
                for (int i = 0; i < overall::kAttrCount; ++i) n_attr += sp.attrs[static_cast<size_t>(i)] != s.pend_before[static_cast<size_t>(i)];
                ImGui::Text("%d attributes change (his %s formula gives %d)", n_attr, position_name(sp.positions[0]), sp.overall);
                ImGui::BeginChild("##popchg", ImVec2(S(440.0f), S(260.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
                if (n_attr == 0) ImGui::TextDisabled("No attribute changes.");
                else attribute_changes_table(s.pend_before, sp.attrs, &archetypes::all()[static_cast<size_t>(u.arch)]);
                ImGui::EndChild();
            }
            const bool go = u.pend_ok && !u.pend_changes.empty();
            if (!go) ImGui::BeginDisabled();
            if (ImGui::Button("Populate##pop")) {
                // the preview is stale when anything it would change has moved since
                const std::vector<PopulateChange> now = populate_changes(app, t, p, u.pend, s.pend_in_career);
                bool same = now.size() == u.pend_changes.size();
                for (size_t i = 0; same && i < now.size(); ++i) same = now[i].field == u.pend_changes[i].field && now[i].was == u.pend_changes[i].was && now[i].to == u.pend_changes[i].to;
                if (!same) {
                    app.notify("Not populated: his data changed since the review (review the changes again)", true);
                } else {
                    char label[200];
                    std::snprintf(label, sizeof(label), "populate %s, overall %d, %s", archetypes::all()[static_cast<size_t>(u.arch)].name, u.target,
                                  ag::tier_name(u.pend.tier));
                    bool back_ok = false;
                    const int n = populate_write(app, t, p, now, label, &back_ok);
                    if (n < 0) {
                        app.notify("Not populated: a field was refused (what was written is back)", true);
                    } else {
                        std::string extra;
                        if (s.pend_squad_role) {
                            if (app.mailbox && !app.busy() &&
                                app.send({{"op", "run"}, {"module", "squad_role"}, {"overrides", {{"playerid", p.playerid}, {"role", u.pend.squad_role}}}},
                                         "Squad role " + p.name))
                                extra = "; squad role sent to the game";
                            else
                                extra = "; squad role NOT sent (another command is running): use Set squad role";
                        }
                        char msg[320];
                        std::snprintf(msg, sizeof(msg), "Populated %s as %s: %d fields written%s (%s; Undo puts them back)", p.name.c_str(),
                                      archetypes::all()[static_cast<size_t>(u.arch)].name, n, extra.c_str(), back_ok ? "read back ok" : "READ-BACK DIFFERS");
                        app.notify(msg, !back_ok);
                        u.applied_playerid = p.playerid;
                        u.applied_label = label;
                        u.applied_note = msg;
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!go) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel##pop")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ================================================================================================================ Create player

bool archetype_populate_create(App& app, const PlayerRow& tmpl, create_form::Form& cur) {
    State& s = st();
    PopulateUiState& u = s.ui;
    u.create_mode = true;
    bool clicked = false;
    ensure_table(app, s);
    ImGui::SeparatorText("Fill the whole player from a player type");
    if (!u.table_ok) {
        ImGui::TextDisabled("The player type list is not available: %s", u.table_error.c_str());
        return false;
    }
    const Table* pt = app.db.table("players");
    overall::Attrs base{};
    bool have_attrs = pt != nullptr;
    for (int i = 0; have_attrs && i < overall::kAttrCount; ++i) {
        have_attrs = cur.has(overall::kAttrNames[i]);
        if (have_attrs) base[static_cast<size_t>(i)] = static_cast<int>(cur.get(overall::kAttrNames[i]));
    }
    if (!have_attrs) {
        ImGui::TextDisabled("Needs all 34 attribute fields; this database lacks some.");
        return false;
    }
    if (s.have_saved && s.saved_template != cur.template_pid) s.have_saved = false;
    if (s.playerid != -cur.template_pid) {
        s.playerid = -cur.template_pid;
        u.target_typed = false;
        u.side = left_position(static_cast<int>(cur.get("preferredposition1", tmpl.position))) ? 1 : 0;
    }
    draw_controls(s, "cp", static_cast<int>(cur.get("preferredposition1", tmpl.position)), static_cast<int>(cur.get("overallrating", tmpl.overall)), false);
    ImGui::SameLine();
    const bool can = u.arch >= 0;
    if (!can) ImGui::BeginDisabled();
    const BridgeState& bs = app.bridge.state();
    if (ImGui::Button("Review changes...###cpopreview") && can) {
        s.pend_playerid = cur.template_pid;
        s.pend_name = "the new player";
        s.pend_before = base;
        s.pend_in_career = bs.in_cm;
        s.pend_squad_role = false;
        s.pend_age = cur.has_birthdate ? age_on(cur.birth, app.today()) : -1;
        const ag::Request rq = make_request(app, s, base, bs.in_cm);
        u.pend_ok = ag::populate(s.table, rq, u.pend);
        const Getter get = [&](const std::string& f) {
            auto it = cur.ints.find(f);
            return it != cur.ints.end() ? it->second : app.db.get_int(*pt, tmpl.rec, f, 0);
        };
        u.pend_changes = u.pend_ok ? changes_against(*pt, get, u.pend, bs.in_cm) : std::vector<PopulateChange>();
        ImGui::OpenPopup("Fill the whole player");
    }
    if (!can) ImGui::EndDisabled();
    if (s.have_saved) {
        ImGui::SameLine();
        if (ImGui::Button("Undo populate###cpopundo")) {
            cur = s.saved;
            s.have_saved = false;
            app.notify("The form is back as it was before Populate");
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Fills the boxes below: positions, attributes, overall, potential, roles, PlayStyles, traits, contract and wage. The squad role can be set once he is created.");
    ImGui::PopTextWrapPos();

    if (ImGui::BeginPopupModal("Fill the whole player", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (s.pend_playerid != cur.template_pid || u.arch < 0) {
            ImGui::TextDisabled("Another template is selected.");
            if (ImGui::Button("Close##cpop")) ImGui::CloseCurrentPopup();
        } else {
            const ag::PlayerSpec& sp = u.pend;
            popup_header(s, "The new player");
            if (!u.pend_ok) {
                ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "Cannot fill him: %s", sp.why.c_str());
                if (sp.closest > 0) {
                    char ub[48];
                    std::snprintf(ub, sizeof(ub), "Use %d##cpopclosest", sp.closest);
                    if (ImGui::Button(ub)) {
                        u.target = sp.closest;
                        u.target_typed = true;
                        const ag::Request rq = make_request(app, s, base, s.pend_in_career);
                        u.pend_ok = ag::populate(s.table, rq, u.pend);
                        const Getter get = [&](const std::string& f) {
                            auto it = cur.ints.find(f);
                            return it != cur.ints.end() ? it->second : app.db.get_int(*pt, tmpl.rec, f, 0);
                        };
                        u.pend_changes = u.pend_ok ? changes_against(*pt, get, u.pend, s.pend_in_career) : std::vector<PopulateChange>();
                    }
                }
            } else {
                for (const std::string& w : sp.warnings) ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", w.c_str());
                auto now_int = [&](const char* f) {
                    auto it = cur.ints.find(f);
                    return it != cur.ints.end() ? it->second : app.db.get_int(*pt, tmpl.rec, f, 0);
                };
                std::vector<int> old_pos, old_roles;
                for (int i = 1; i <= 7; ++i) old_pos.push_back(static_cast<int>(now_int(("preferredposition" + std::to_string(i)).c_str())));
                for (int i = 1; i <= 9; ++i) old_roles.push_back(static_cast<int>(now_int(("role" + std::to_string(i)).c_str())));
                row("Positions", position_words(old_pos), position_words(sp.positions));
                row("Roles", role_words(old_roles), role_words(sp.roles));
                auto words = [&](int64_t t1, int64_t t2) {
                    std::vector<std::string> v = bit_words(t1, playstyle1_names(), 0, 30);
                    const std::vector<std::string> g = bit_words(t2, playstyle2_names(), 0, 6);
                    v.insert(v.end(), g.begin(), g.end());
                    return v;
                };
                row("PlayStyles", join(words(now_int("trait1"), now_int("trait2"))), join(words(sp.trait1, sp.trait2)));
                row("PlayStyles+", join(words(now_int("icontrait1"), now_int("icontrait2"))), join(words(sp.icontrait1, sp.icontrait2)));
                row("Traits", join(bit_words(now_int("trait2"), playstyle2_names(), 6, 17)),
                    join(bit_words((now_int("trait2") & ~kManaged2) | sp.trait2, playstyle2_names(), 6, 17)));
                row("Overall", num(now_int("overallrating")), num(sp.overall));
                row("Potential", num(now_int("potential")), num(sp.potential));
                if (s.pend_in_career && sp.contract_until > 0) {
                    row("Contract until", num(now_int("contractvaliduntil")), num(sp.contract_until));
                    row("Wage", num(now_int("wage")), num(sp.wage));
                }
                if (sp.max_age > 0 && s.pend_age > sp.max_age)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "Born so that he is %d: Populate makes him 19 (a prospect is %d at most).", s.pend_age, sp.max_age);
                ImGui::TextDisabled("Squad role %d (%s): set it on his Edit page once he is created.", sp.squad_role, ag::tier_name(sp.tier));
                int n_attr = 0;
                for (int i = 0; i < overall::kAttrCount; ++i) n_attr += sp.attrs[static_cast<size_t>(i)] != base[static_cast<size_t>(i)];
                ImGui::Text("%d attributes change (his %s formula gives %d)", n_attr, position_name(sp.positions[0]), sp.overall);
                ImGui::BeginChild("##cpopchg", ImVec2(S(440.0f), S(240.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
                if (n_attr == 0) ImGui::TextDisabled("No attribute changes.");
                else attribute_changes_table(base, sp.attrs, &archetypes::all()[static_cast<size_t>(u.arch)]);
                ImGui::EndChild();
            }
            const bool go = u.pend_ok && !u.pend_changes.empty();
            if (!go) ImGui::BeginDisabled();
            if (ImGui::Button("Populate##cpop")) {
                s.saved = cur;
                s.saved_template = cur.template_pid;
                s.have_saved = true;
                for (const PopulateChange& c : u.pend_changes) cur.ints[c.field] = c.to;
                if (u.pend.max_age > 0 && cur.has_birthdate && s.pend_age > u.pend.max_age) {
                    const GameDate today = app.today();
                    if (today.valid()) {
                        cur.birth.year = today.year - 19;
                        cur.birth.day = std::min(cur.birth.day, 28);
                    }
                }
                clicked = true;
                u.applied_label = "form";
                app.notify(std::string("The form is filled as ") + archetypes::all()[static_cast<size_t>(u.arch)].name + " (Undo populate puts it back)");
                ImGui::CloseCurrentPopup();
            }
            if (!go) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel##cpop")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return clicked;
}

}  // namespace turbo
