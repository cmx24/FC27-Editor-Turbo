// FC 27 LE Turbo GUI - 2.20 K3: the club, crowd, stadium and kit fields (see ui_club_fields.h, core/club_fields.h)
#include "ui_club_fields.h"

#include <cfloat>
#include <deque>
#include <set>

#include "app.h"
#include "core/club_fields.h"
#include "imgui.h"
#include "ui_theme.h"

namespace turbo {

namespace cf = clubfields;

namespace {

// ------------------------------------------------------------------ the undo list of these sections
struct Step {
    std::string table, field, title;
    uint64_t rec = 0;
    int64_t team = 0;
    int64_t before = 0, after = 0;
};
std::deque<Step> g_undo;
constexpr size_t kUndoMax = 40;

int64_t team_of_row(App& app, const Table& t, uint64_t rec) {
    if (t.name == "teams") return app.db.get_int(t, rec, "teamid", 0);
    if (t.name == "teamkits") return app.db.get_int(t, rec, "teamtechid", 0);
    return 0;
}

std::string label_or_other(const cf::Def& d, int64_t v) {
    const std::string t = cf::value_label(d, static_cast<int>(v)).text;
    return t.empty() ? cf::other_label(v) : t;
}

// ------------------------------------------------------------------ one picker
constexpr float kLabelCol = 190.0f;
constexpr float kCtlWidth = 250.0f;

void title_cell(const cf::Def& d) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(d.title);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n(%s)", d.help, d.field);
    ImGui::SameLine(S(kLabelCol));
}

// Returns true when a write was asked for (value in *pick)
bool field_row(App& app, const Table& t, uint64_t rec, const cf::Def& d, int64_t team, int64_t* pick) {
    const Field* f = t.field(d.field);
    if (!f || f->type != FieldType::Int) return false;  // this build's database has no such column: nothing to draw
    Value v;
    const bool readable = app.db.get(t, rec, *f, v);
    const std::vector<int> set = cf::values_of(d);
    ImGui::PushID(d.field);
    title_cell(d);
    bool asked = false;
    if (!readable || set.empty()) {
        ImGui::TextDisabled("unreadable");
    } else if (d.look == cf::Look::Switch && set.size() == 2 && (v.i == 0 || v.i == 1)) {
        // a plain On / Off: a tick box with the word beside it
        bool on = v.i == 1;
        if (ImGui::Checkbox("##cf", &on)) {
            *pick = on ? 1 : 0;
            asked = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(on ? "On" : "Off");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", cf::value_hover(d, static_cast<int>(v.i)).c_str());
    } else if (set.size() == 1 && v.i == set[0]) {
        // the game's data holds one value only: shown, locked
        ImGui::BeginDisabled();
        ImGui::TextUnformatted(label_or_other(d, v.i).c_str());
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s\nThe game's data holds this one value for every stadium, so there is nothing to choose.",
                              cf::value_hover(d, static_cast<int>(v.i)).c_str());
    } else {
        const std::string cur = label_or_other(d, v.i);
        ImGui::SetNextItemWidth(S(kCtlWidth));
        if (ImGui::BeginCombo("##cf", cur.c_str())) {
            for (int c : set) {
                const std::string txt = cf::value_label(d, c).text;
                if (ImGui::Selectable((txt + "##v" + std::to_string(c)).c_str(), v.i == c) && v.i != c) {
                    *pick = c;
                    asked = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cf::value_hover(d, c).c_str());
                if (v.i == c && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", cf::value_hover(d, static_cast<int>(v.i)).c_str());
    }
    ImGui::PopID();
    (void)team;
    return asked;
}

// the in-app Log and turbo_gui.log (the Tester's live check reads the file)
void log_line(App& app, const std::string& line) {
    app.log(line);
    if (app.log_hook) app.log_hook(line);
}

void undo_line(App& app, int64_t team, const char* id) {
    const bool mine = !g_undo.empty() && g_undo.back().team == team;
    ImGui::BeginDisabled(!mine);
    if (ImGui::SmallButton((std::string("Undo last change##cfundo_") + id).c_str())) {
        std::string msg;
        const bool ok = undo_club_field(app, &msg);
        app.notify(msg, !ok);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (mine) {
            const Step& s = g_undo.back();
            const cf::Def* d = cf::find_def(s.table, s.field);
            ImGui::SetTooltip("Puts \"%s\" back to %s.", s.title.c_str(), d ? label_or_other(*d, s.before).c_str() : std::to_string(s.before).c_str());
        } else {
            ImGui::SetTooltip("Nothing changed here for this club yet.");
        }
    }
}

// the defs of one section, grouped by their sub-heading in the order the list has them
void draw_defs(App& app, const Table& t, uint64_t rec, cf::Section sec, int64_t team) {
    std::string group;
    for (const cf::Def& d : cf::defs()) {
        if (d.section != sec || !d.editable) continue;
        if (!t.field(d.field)) continue;
        if (group != d.group) {
            group = d.group;
            ImGui::SeparatorText(d.group);
        }
        int64_t pick = 0;
        if (field_row(app, t, rec, d, team, &pick)) {
            std::string msg;
            if (!write_club_field(app, t, rec, d.field, pick, &msg, team)) app.notify(msg, true);
        }
    }
}

// the home ground of a club, kept for one database generation
struct HgCache {
    int64_t team = -1;
    int gen = -1;
    HomeGround hg;
};
HgCache g_hg;

}  // namespace

// ================================================================== writes
bool write_club_field(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t value, std::string* msg, int64_t team) {
    auto fail = [&](const std::string& why) {
        if (msg) *msg = why;
        return false;
    };
    const cf::Def* d = cf::find_def(t.name, field);
    if (!d) return fail(field + " is not one of the club fields Turbo draws here");
    const Field* f = t.field(field);
    if (!f || f->type != FieldType::Int) return fail(std::string(d->title) + " is not in this game's database");
    const std::string why = cf::refusal(*d, value);
    if (!why.empty()) return fail(why);
    const std::string verr = Database::validate(*f, Value::of_int(value));
    if (!verr.empty()) return fail(std::string(d->title) + ": " + verr);
    if (!app.db.table_alive(t, rec)) return fail("The database was reloaded: open the page again");
    Value b;
    if (!app.db.get(t, rec, *f, b)) return fail(std::string(d->title) + " could not be read");
    if (b.i == value) {
        if (msg) *msg = std::string(d->title) + " is already " + label_or_other(*d, value);
        return true;
    }
    if (!app.edit(t, rec, *f, Value::of_int(value))) return fail(std::string("Writing ") + d->title + " failed");
    const int64_t back = app.db.get_int(t, rec, field, INT64_MIN);
    const int64_t who = team > 0 ? team : team_of_row(app, t, rec);
    // the line the live check reads (tools/club_fields_livecheck.py): what was written and what the database holds afterwards
    log_line(app, "Club field: " + t.name + "." + field + " team " + std::to_string(who) + " = " + std::to_string(value) + " (was " +
            std::to_string(b.i) + "), read back " + (back == INT64_MIN ? std::string("nothing") : std::to_string(back)));
    if (back != value) return fail(std::string(d->title) + " was written but the game's memory reads " + std::to_string(back));
    Step s;
    s.table = t.name;
    s.field = field;
    s.title = d->title;
    s.rec = rec;
    s.team = who;
    s.before = b.i;
    s.after = value;
    g_undo.push_back(s);
    while (g_undo.size() > kUndoMax) g_undo.pop_front();
    if (msg) *msg = std::string(d->title) + ": " + label_or_other(*d, value);
    return true;
}

bool undo_club_field(App& app, std::string* msg) {
    if (g_undo.empty()) {
        if (msg) *msg = "Nothing to undo.";
        return false;
    }
    const Step s = g_undo.back();
    g_undo.pop_back();
    auto fail = [&](const std::string& why) {
        if (msg) *msg = why;
        return false;
    };
    const Table* t = app.db.table(s.table);
    const Field* f = t ? t->field(s.field) : nullptr;
    if (!t || !f) return fail("That field is not in the database any more.");
    if (!app.db.table_alive(*t, s.rec)) return fail("The database was reloaded: that change can no longer be undone.");
    const std::string verr = Database::validate(*f, Value::of_int(s.before));
    if (!verr.empty()) return fail(verr);
    if (!app.edit(*t, s.rec, *f, Value::of_int(s.before))) return fail("Writing " + s.title + " back failed.");
    const int64_t back = app.db.get_int(*t, s.rec, s.field, INT64_MIN);
    log_line(app, "Club field undone: " + s.table + "." + s.field + " team " + std::to_string(s.team) + " = " + std::to_string(s.before) +
            ", read back " + (back == INT64_MIN ? std::string("nothing") : std::to_string(back)));
    if (back != s.before) return fail(s.title + " was written back but the game's memory reads " + std::to_string(back));
    const cf::Def* d = cf::find_def(s.table, s.field);
    if (msg) *msg = "Undone: " + s.title + " is " + (d ? label_or_other(*d, s.before) : std::to_string(s.before)) + " again.";
    return true;
}

size_t club_field_undo_depth() { return g_undo.size(); }
int64_t club_field_undo_top_team() { return g_undo.empty() ? 0 : g_undo.back().team; }
void club_field_undo_clear() { g_undo.clear(); }

// ================================================================== the home ground
HomeGround home_ground_of(App& app, int64_t teamid) {
    if (g_hg.team == teamid && g_hg.gen == app.gen) return g_hg.hg;
    HomeGround hg;
    g_hg.team = teamid;
    g_hg.gen = app.gen;
    const Table* lt = app.db.table("teamstadiumlinks");
    const Table* st = app.db.table("stadiums");
    if (!lt || !st || !lt->has("teamid") || !lt->has("stadiumid") || !st->has("stadiumid")) {
        hg.why = "This game's database has no stadium list.";
        g_hg.hg = hg;
        return hg;
    }
    Snapshot ls;
    if (!ls.load(app.db.memory(), *lt)) {
        hg.why = "The stadium links could not be read.";
        g_hg.hg = hg;
        return hg;
    }
    const Field* ltf = lt->field("teamid");
    const Field* lsf = lt->field("stadiumid");
    int64_t sid = -1;
    for (uint32_t i : ls.valid)
        if (ls.get_int(i, *ltf) == teamid) {
            sid = ls.get_int(i, *lsf);
            break;
        }
    if (sid < 0) {
        hg.why = "This club has no home ground in the database.";
        g_hg.hg = hg;
        return hg;
    }
    std::set<int64_t> others;
    for (uint32_t i : ls.valid)
        if (ls.get_int(i, *lsf) == sid && ls.get_int(i, *ltf) != teamid) others.insert(ls.get_int(i, *ltf));
    Snapshot ss;
    if (!ss.load(app.db.memory(), *st)) {
        hg.why = "The stadiums could not be read.";
        g_hg.hg = hg;
        return hg;
    }
    const Field* sidf = st->field("stadiumid");
    const Field* namef = st->field("name");
    for (uint32_t i : ss.valid) {
        if (ss.get_int(i, *sidf) != sid) continue;
        hg.found = true;
        hg.rec = ss.addr(i);
        hg.stadiumid = sid;
        hg.shared_with = static_cast<int>(others.size());
        if (namef && namef->type == FieldType::String) hg.name = ss.get_str(i, *namef);
        if (hg.name.empty()) hg.name = "Stadium " + std::to_string(sid);
        break;
    }
    if (!hg.found) hg.why = "The home ground (stadium " + std::to_string(sid) + ") is not in the stadium list.";
    g_hg.hg = hg;
    return hg;
}

// ================================================================== the sections
void draw_club_crowd_section(App& app, const Table& teams, uint64_t rec) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Crowd & presentation###cfcrowd")) return;
    const int64_t team = team_of_row(app, teams, rec);
    ImGui::PushID("cfcrowdsec");
    draw_defs(app, teams, rec, cf::Section::Crowd, team);
    ImGui::Spacing();
    undo_line(app, team, "crowd");
    ImGui::TextDisabled("Each choice is written to the career at once. The game uses crowd and presentation changes when a match is loaded.");
    ImGui::TextDisabled("Not proven yet: whether they show in a match, and whether they survive saving and loading the career.");
    ImGui::PopID();
}

void draw_home_ground_section(App& app, int64_t teamid) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Home ground###cfhome")) return;
    const HomeGround hg = home_ground_of(app, teamid);
    ImGui::PushID("cfhomesec");
    if (!hg.found) {
        ImGui::TextDisabled("%s", hg.why.c_str());
        ImGui::PopID();
        return;
    }
    const Table* st = app.db.table("stadiums");
    if (!st || !app.db.table_alive(*st, hg.rec)) {
        ImGui::TextDisabled("The stadium list was reloaded: open the page again.");
        ImGui::PopID();
        return;
    }
    ImGui::Text("Stadium: %s", hg.name.c_str());
    if (hg.shared_with > 0)
        theme::warn_text("These settings belong to the stadium, so %d other club%s that play%s there get them too.", hg.shared_with,
                         hg.shared_with == 1 ? "" : "s", hg.shared_with == 1 ? "s" : "");
    draw_defs(app, *st, hg.rec, cf::Section::HomeGround, teamid);
    ImGui::Spacing();
    undo_line(app, teamid, "home");
    ImGui::TextDisabled("Each choice is written to the career at once. The game reads the stadium again when a match is loaded.");
    ImGui::TextDisabled("Not proven yet: whether the choices show in a match, and whether they survive saving and loading the career.");
    ImGui::PopID();
}

void draw_kit_extras_section(App& app, const Table& teamkits, uint64_t rec) {
    const int64_t team = team_of_row(app, teamkits, rec);
    ImGui::PushID("cfkitsec");
    draw_defs(app, teamkits, rec, cf::Section::KitExtras, team);
    ImGui::Spacing();
    undo_line(app, team, "kit");
    ImGui::TextDisabled("These are written to the career at once but last only until the next career load: the game reads its kits again then.");
    ImGui::PopID();
}

}  // namespace turbo
