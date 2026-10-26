// FC 27 LE Turbo GUI - Players > Bulk edit (see ui_bulk_edit.h). NEW file; calls the editors and helpers that already exist:
// App::edit / App::edit_field (the validated write of the Players editor), the club and nation pickers, the code lists of widgets.cpp,
// the plain titles of core/field_labels.h. Not registered with the shell.
#include "ui_bulk_edit.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "app.h"
#include "core/field_labels.h"
#include "core/model.h"
#include "core/overall.h"
#include "core/range_filter.h"
#include "core/team_preset.h"
#include "geo.h"
#include "imgui.h"
#include "playstyles.h"
#include "ui_career_page.h"  // TURBO_WEAK
#include "ui_page.h"
#include "ui_team_filter.h"
#include "ui_theme.h"

namespace turbo {

// The "My team preset" (core/team_preset.h of 2.20/team-defaults): a weak reference, null while the build does not have it.
bool team_preset_apply(App& app, int64_t teamid) TURBO_WEAK;

namespace bulkedit {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------- state
struct Filter {
    char search[64] = "";
    std::vector<int64_t> clubs;  // the clubs picked (their squads, loaned-in players included)
    char club_search[64] = "";
    int64_t club_pick = 0;
    bool my_club = false, free_agents = false, retiring = false;
    int pos = -1, gender = -1;
    int64_t nat = -1;
    IntRange age, ovr, pot;
};

struct UndoRec {
    int64_t pid = 0;
    std::vector<std::pair<std::string, int64_t>> before;  // field, value it held before the run
};

// A finished run that can be put back
struct Done {
    std::string key;    // the loaded database it was made in
    std::string what;   // "Jersey fit"
    std::vector<std::string> shown;  // the chosen fields (their put-back is logged for small runs)
    std::vector<UndoRec> recs;
};

struct Run {
    bool active = false, undoing = false;
    size_t next = 0;
    std::vector<int64_t> pids;
    std::vector<Row> rows;
    std::vector<std::string> snap;  // every field whose value is read before / after (the chosen ones, and what an overall write moves)
    std::vector<UndoRec> recs;
    std::vector<UndoRec> back;      // undo: the records to put back (newest last)
    size_t changed = 0, values = 0, failed = 0, same = 0;
    std::string what, key;
    std::vector<std::string> shown;  // the chosen fields
    bool verbose = false;            // small runs log what each player reads back (the live check compares it)
};

Filter g_f;
std::vector<Row> g_rows;
std::vector<int64_t> g_matched;
std::unordered_set<int64_t> g_off;  // ticked off by hand
std::string g_key;
uint64_t g_version = ~uint64_t(0);
bool g_confirm_big = false;
bool g_do_fit = false, g_do_form = false, g_do_morale = false;
int g_fit = 95, g_form = 100, g_morale = 100;
// Development (re-homed from Lab > Bulk edit, ui_tools.cpp section_bulk_edit): every box starts OFF (the core/team_preset.h growth defaults)
bool g_do_dev = teampreset::kExtraXpDefault;
bool g_no_decline = teampreset::kNoDeclineDefault;
int g_bonus_xp = 0;
float g_xp_mult = 2.0f;
Run g_run;
std::vector<Done> g_done;  // newest last
State g_state;

constexpr size_t kBigRun = 300;       // above this the run asks for a tick first
constexpr size_t kPerFrame = 40;      // players handled per frame
constexpr size_t kLogReadBack = 20;  // runs up to this many players log one read-back line per player and field
constexpr size_t kMaxDone = 8;        // runs that can be put back
constexpr size_t kSendPlayers = 350;  // players one fitness / form / morale command can name

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains_ci(const std::string& hay, const std::string& needle_lower) {
    return needle_lower.empty() || lower(hay).find(needle_lower) != std::string::npos;
}

std::string load_key(App& app) {
    const BridgeState& st = app.bridge.state();
    return st.session + "#" + std::to_string(st.load_gen) + (st.in_cm ? "c" : "m");
}

const Table* players_table(App& app) { return app.db.table("players"); }

// ---------------------------------------------------------------- fields
enum class Control { Switch, Date, Nation, Position, Code, Bits, Number };

bool is_bits_field(const std::string& n) { return n == "trait1" || n == "icontrait1" || n == "trait2" || n == "icontrait2"; }
const std::vector<const char*>& bit_names(const std::string& n) { return n == "trait2" || n == "icontrait2" ? playstyle2_names() : playstyle1_names(); }

Control control_of(const Field& f) {
    if (f.depth == 1 && f.min == 0) return Control::Switch;
    if (f.name == "birthdate" || f.name == "playerjointeamdate") return Control::Date;
    if (f.name == "nationality") return Control::Nation;
    if (labels::is_position_field("players", f.name)) return Control::Position;
    if (is_bits_field(f.name)) return Control::Bits;
    if (is_enum_field(f.name)) return Control::Code;
    return Control::Number;
}

// A name id is not a value anyone sets for a group; the player's key is the player
bool excluded_field(const std::string& n) {
    return n == "playerid" || n == "firstnameid" || n == "lastnameid" || n == "commonnameid" || n == "playerjerseynameid";
}

bool moves_overall(const std::string& n) {
    return n == "overallrating" || n == "preferredposition1" || overall::attr_index(n) >= 0;
}

const char* kGroupOrder[] = {"Rating", "Attributes", "Position", "PlayStyles", "Contract", "Looks", "Other"};

bool looks_field(const std::string& n) {
    static const char* prefixes[] = {"head", "hair", "facial", "eye", "skin", "tattoo", "sock", "shoe", "jersey", "glove", "gkglove",
                                     "accessory", "body", "sideburns", "lip", "nose", "ear", "hasseasonal", "hashigh", "animfreekick",
                                     "animpenalties", "runningcode", "faceposer", "smallsided", "shortstyle", "undershortstyle", "muscularity",
                                     "outfit", "runstyle"};
    for (const char* p : prefixes)
        if (n.compare(0, std::strlen(p), p) == 0) return true;
    return false;
}

size_t group_rank(const std::string& g) {
    for (size_t i = 0; i < sizeof(kGroupOrder) / sizeof(kGroupOrder[0]); ++i)
        if (g == kGroupOrder[i]) return i;
    return 99;
}

std::string group_of(const std::string& n) {
    if (overall::attr_index(n) >= 0) return "Attributes";
    if (n == "overallrating" || n == "potential" || n == "modifier" || n == "growthprofile") return "Rating";
    if (labels::is_position_field("players", n)) return "Position";
    if (is_bits_field(n)) return "PlayStyles";
    if (n == "contractvaliduntil" || n == "wage" || n == "releaseclause" || n == "playerjointeamdate" || n == "jerseynumber") return "Contract";
    if (looks_field(n)) return "Looks";
    return "Other";
}

std::vector<std::string> list_fields(const Table& t) {
    struct E {
        std::string name, title, group;
    };
    std::vector<E> v;
    for (const Field& f : t.fields) {
        if (f.type != FieldType::Int || excluded_field(f.name) || f.depth <= 0) continue;
        v.push_back({f.name, field_label(f.name), group_of(f.name)});
    }
    std::stable_sort(v.begin(), v.end(), [](const E& a, const E& b) {
        const size_t ra = group_rank(a.group), rb = group_rank(b.group);
        if (ra != rb) return ra < rb;
        return lower(a.title) < lower(b.title);
    });
    std::vector<std::string> out;
    for (const E& e : v) out.push_back(e.name);
    return out;
}

int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

Row* find_row(const std::string& field) {
    for (Row& r : g_rows)
        if (r.field == field) return &r;
    return nullptr;
}

// what a row writes, in words, for the preview ("Jersey fit Normal -> Tight")
std::string describe_row(App& app, const Table& t, const Field& f, const Row& r, int64_t cur, int64_t nv) {
    const std::string title = field_label(f.name);
    if (r.mode == Mode::Give || r.mode == Mode::Take) {
        const auto& names = bit_names(f.name);
        const char* n = r.bit >= 0 && r.bit < static_cast<int>(names.size()) && names[static_cast<size_t>(r.bit)] ? names[static_cast<size_t>(r.bit)] : "that PlayStyle";
        return title + ": " + (r.mode == Mode::Give ? "gets " : "loses ") + n;
    }
    return title + " " + app.describe_value(t, f, Value::of_int(cur)) + " -> " + app.describe_value(t, f, Value::of_int(nv));
}

// ---------------------------------------------------------------- the players
bool passes(const PlayerRow& p, const Filter& f, int64_t user, const std::unordered_set<int64_t>* in_clubs,
            const std::unordered_map<int64_t, int64_t>* nat, const std::string& q, bool numeric, int64_t qid, bool pos_exact) {
    if (f.my_club && f.clubs.empty()) {
        if (p.club != user) return false;
    } else if (f.my_club || !f.clubs.empty()) {
        const bool mine = f.my_club && p.club == user;
        const bool picked = in_clubs && in_clubs->count(p.playerid) > 0;
        if (!mine && !picked) return false;
    }
    if (f.free_agents && p.club != 0) return false;
    if (f.retiring && !p.retiring) return false;
    if (f.gender >= 0 && p.gender != f.gender) return false;
    if (f.pos >= 0 && !player_passes_position(p, f.pos, pos_exact)) return false;
    if (f.nat >= 0) {
        const auto n = nat ? nat->find(p.playerid) : std::unordered_map<int64_t, int64_t>::const_iterator();
        if (!nat || n == nat->end() || n->second != f.nat) return false;
    }
    if (!range_passes(f.ovr, p.overall) || !range_passes(f.pot, p.potential) || !range_passes(f.age, p.age, p.age >= 0)) return false;
    if (!q.empty()) {
        if (numeric) {
            if (p.playerid != qid && std::to_string(p.playerid).find(q) != 0) return false;
        } else if (lower(p.name).find(q) == std::string::npos && lower(p.club_name).find(q) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::string filter_key(App& app) {
    char b[320];
    std::snprintf(b, sizeof(b), "%s|%d%d%d|%d|%d|%lld|%d-%d|%d-%d|%d-%d|%lld|%d", lower(g_f.search).c_str(), g_f.my_club ? 1 : 0, g_f.free_agents ? 1 : 0,
                  g_f.retiring ? 1 : 0, g_f.pos, g_f.gender, static_cast<long long>(g_f.nat), g_f.age.from, g_f.age.to, g_f.ovr.from, g_f.ovr.to,
                  g_f.pot.from, g_f.pot.to, static_cast<long long>(app.bridge.state().user_team), position_show_all() ? 1 : 0);
    std::string k = b;
    for (int64_t c : g_f.clubs) k += "," + std::to_string(c);
    return k;
}

void rebuild_matched(App& app) {
    g_matched.clear();
    const int64_t user = app.bridge.state().user_team;
    std::unordered_set<int64_t> in_clubs;
    for (int64_t c : g_f.clubs)
        for (const LinkRow& l : app.model.links_of_team(c)) in_clubs.insert(l.playerid);
    std::unordered_map<int64_t, int64_t> nat;
    if (g_f.nat >= 0) {
        if (const Table* t = players_table(app); t && t->has("nationality")) {
            Snapshot s;
            if (s.load(app.db.memory(), *t))
                for (uint32_t i : s.valid) nat[s.get_int(i, "playerid")] = s.get_int(i, "nationality", -1);
        }
    }
    const std::string q = lower(g_f.search);
    const bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), [](char c) { return c >= '0' && c <= '9'; });
    const int64_t qid = numeric ? std::atoll(q.c_str()) : 0;
    const bool pos_exact = position_show_all();
    std::vector<const PlayerRow*> rows;
    for (const PlayerRow& p : app.model.players())
        if (passes(p, g_f, user, &in_clubs, &nat, q, numeric, qid, pos_exact)) rows.push_back(&p);
    std::stable_sort(rows.begin(), rows.end(), [](const PlayerRow* a, const PlayerRow* b) { return lower(a->name) < lower(b->name); });
    for (const PlayerRow* r : rows) g_matched.push_back(r->playerid);
}

void refresh_matched(App& app) {
    if (g_run.active) return;  // the list stays as it was while a run writes (each write changes the model version)
    const std::string key = filter_key(app);
    if (key != g_key || g_version != app.model.version()) {
        g_key = key;
        g_version = app.model.version();
        rebuild_matched(app);
    }
}

std::vector<int64_t> selected_ids() {
    std::vector<int64_t> out;
    out.reserve(g_matched.size());
    for (int64_t id : g_matched)
        if (!g_off.count(id)) out.push_back(id);
    return out;
}

// ---------------------------------------------------------------- the run
// A write through App::edit can toast for every player (the overall follows, a refusal): the run keeps one refusal and drops the rest
void trim_toasts(App& app, size_t from) {
    if (app.toasts.size() <= from) return;
    bool kept = false;
    for (size_t i = from; i < app.toasts.size();) {
        if (!app.toasts[i].error || kept) app.toasts.erase(app.toasts.begin() + static_cast<long>(i));
        else {
            kept = true;
            ++i;
        }
    }
}

std::string what_of(const std::vector<Row>& rows) {
    std::string w;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i == 3) {
            w += ", ...";
            break;
        }
        w += (i ? ", " : "") + field_label(rows[i].field);
    }
    return w;
}

std::vector<std::string> snapshot_fields(const std::vector<Row>& rows) {
    std::vector<std::string> v;
    bool wide = false;
    for (const Row& r : rows) {
        v.push_back(r.field);
        wide = wide || moves_overall(r.field);
    }
    if (wide) {
        for (int i = 0; i < overall::kAttrCount; ++i) v.push_back(overall::kAttrNames[i]);
        v.push_back("overallrating");
        v.push_back("preferredposition1");
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

void apply_player(App& app, const Table& t, int64_t pid, Run& r) {
    const PlayerRow* p = app.model.player(pid);
    if (!p || !app.db.table_alive(t, p->rec)) {
        ++r.failed;
        return;
    }
    const uint64_t rec = p->rec;
    std::vector<const Field*> snap;
    std::vector<int64_t> before;
    for (const std::string& n : r.snap)
        if (const Field* f = t.field(n)) {
            snap.push_back(f);
            before.push_back(app.db.get_int(t, rec, n, 0));
        }
    const size_t toasts0 = app.toasts.size();
    bool refused = false;
    for (const Row& row : r.rows) {
        const Field* f = t.field(row.field);
        if (!f) continue;
        const int64_t cur = app.db.get_int(t, rec, row.field, 0);
        const int64_t nv = new_value(row, cur, f->min, f->max());
        if (nv == cur) continue;
        if (!app.edit(t, rec, *f, Value::of_int(nv))) refused = true;
    }
    trim_toasts(app, toasts0);
    if (r.verbose) {
        for (const Row& row : r.rows) {
            const Field* f = t.field(row.field);
            if (!f) continue;
            const int64_t now = app.db.get_int(t, rec, row.field, 0);
            app.log("Bulk edit read back (" + app.model.player_name(pid) + "): " + field_label(row.field) + " = " +
                    app.describe_value(t, *f, Value::of_int(now)) + "  [players." + row.field + " = " + std::to_string(now) + "]");
        }
    }
    UndoRec u;
    u.pid = pid;
    size_t chosen_changed = 0;
    for (size_t i = 0; i < snap.size(); ++i) {
        const int64_t now = app.db.get_int(t, rec, snap[i]->name, 0);
        if (now == before[i]) continue;
        u.before.push_back({snap[i]->name, before[i]});
        for (const Row& row : r.rows)
            if (row.field == snap[i]->name) ++chosen_changed;
    }
    if (refused) ++r.failed;
    if (u.before.empty()) {
        if (!refused) ++r.same;
        return;
    }
    r.recs.push_back(std::move(u));
    ++r.changed;
    r.values += chosen_changed;
}

void undo_player(App& app, const Table& t, const UndoRec& u, Run& r) {
    const PlayerRow* p = app.model.player(u.pid);
    if (!p || !app.db.table_alive(t, p->rec)) {
        ++r.failed;
        return;
    }
    const size_t toasts0 = app.toasts.size();
    bool ok = true;
    for (auto it = u.before.rbegin(); it != u.before.rend(); ++it) {
        const Field* f = t.field(it->first);
        if (!f || !app.edit_field(t, p->rec, *f, Value::of_int(it->second), false)) ok = false;
    }
    trim_toasts(app, toasts0);
    if (r.verbose) {
        for (const auto& b : u.before) {
            if (std::find(r.shown.begin(), r.shown.end(), b.first) == r.shown.end()) continue;
            const Field* f = t.field(b.first);
            if (!f) continue;
            const int64_t now = app.db.get_int(t, p->rec, b.first, 0);
            app.log("Bulk edit put back (" + app.model.player_name(u.pid) + "): " + field_label(b.first) + " = " +
                    app.describe_value(t, *f, Value::of_int(now)) + "  [players." + b.first + " = " + std::to_string(now) + "]");
        }
    }
    if (ok) ++r.changed;
    else ++r.failed;
}

void finish_run(App& app) {
    Run& r = g_run;
    r.active = false;
    char b[256];
    if (r.undoing) {
        std::snprintf(b, sizeof(b), "Put back %zu players%s.", r.changed, r.failed ? " (some could not be put back)" : "");
        g_state.result = b;
        app.notify(g_state.result, r.failed > 0);
    } else {
        if (r.changed > 0) {
            Done d;
            d.key = r.key;
            d.what = r.what;
            d.shown = r.shown;
            d.recs = std::move(r.recs);
            g_done.push_back(std::move(d));
            while (g_done.size() > kMaxDone) g_done.erase(g_done.begin());
        }
        std::snprintf(b, sizeof(b), "Changed %zu players (%zu values)%s%s.", r.changed, r.values,
                      r.same ? ("; " + std::to_string(r.same) + " already had it").c_str() : "",
                      r.failed ? ("; " + std::to_string(r.failed) + " refused").c_str() : "");
        g_state.result = b;
        app.notify(g_state.result, r.failed > 0 && r.changed == 0);
    }
    r.recs.clear();
    r.back.clear();
}

// One slice of the run: kPerFrame players. A run that keeps being refused stops after three players.
void step_run(App& app, size_t budget) {
    Run& r = g_run;
    if (!r.active) return;
    const Table* t = players_table(app);
    if (!t || load_key(app) != r.key) {
        r.active = false;
        g_state.result = "Stopped: the game loaded another save.";
        app.notify(g_state.result, true);
        return;
    }
    const size_t total = r.undoing ? r.back.size() : r.pids.size();
    for (size_t n = 0; n < budget && r.next < total; ++n, ++r.next) {
        if (r.undoing) undo_player(app, *t, r.back[r.back.size() - 1 - r.next], r);
        else apply_player(app, *t, r.pids[r.next], r);
        if (r.failed >= 3) {
            r.next = total;
            break;
        }
    }
    g_state.done = r.next;
    g_state.total = total;
    if (r.next >= total) finish_run(app);
}

// ---------------------------------------------------------------- why Apply is greyed out, and the preview
std::string why_not_apply(App& app, const std::vector<int64_t>& sel) {
    if (g_run.active) return "a run is in progress";
    if (g_rows.empty()) return "add a field to change";
    if (sel.empty()) return "no player is picked";
    const Table* t = players_table(app);
    if (!t) return "the players are not loaded";
    for (const Row& r : g_rows) {
        const Field* f = t->field(r.field);
        if (!f) return field_label(r.field) + " is not in this database";
        if (!r.value_chosen) return "choose a value for " + field_label(r.field);
        if (control_of(*f) == Control::Date && r.mode == Mode::Set) {
            if (!is_real_date(GameDate{r.ymd[0], r.ymd[1], r.ymd[2]})) return field_label(r.field) + ": that is not a date";
        }
    }
    if (sel.size() > kBigRun && !g_confirm_big) return "tick the box to change this many players";
    return "";
}

void build_preview(App& app, const std::vector<int64_t>& sel) {
    g_state.preview.clear();
    const Table* t = players_table(app);
    if (!t) return;
    size_t shown = 0;
    for (int64_t pid : sel) {
        if (shown >= 3) break;
        const PlayerRow* p = app.model.player(pid);
        if (!p) continue;
        std::string line = p->name + ": ";
        bool any = false;
        for (const Row& r : g_rows) {
            const Field* f = t->field(r.field);
            if (!f || !r.value_chosen) continue;
            const int64_t cur = app.db.get_int(*t, p->rec, r.field, 0);
            const int64_t nv = new_value(r, cur, f->min, f->max());
            if (nv == cur && r.mode != Mode::Give && r.mode != Mode::Take) continue;
            line += (any ? "; " : "") + describe_row(app, *t, *f, r, cur, nv);
            any = true;
        }
        if (!any) line += "nothing to change";
        g_state.preview.push_back(line);
        ++shown;
    }
}

// ---------------------------------------------------------------- pickers
const char* kModeSet = "Set to";
const char* kModeAdd = "Change by";

bool mode_combo(const char* id, Mode& m, const char* a, const char* b, Mode ma, Mode mb) {
    bool changed = false;
    ImGui::SetNextItemWidth(S(110.0f));
    if (ImGui::BeginCombo(id, m == ma ? a : b)) {
        if (ImGui::Selectable(a, m == ma) && m != ma) {
            m = ma;
            changed = true;
        }
        if (ImGui::Selectable(b, m == mb) && m != mb) {
            m = mb;
            changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

void pick_code(App& app, const Table& t, const Field& f, Row& r, float width) {
    ImGui::SetNextItemWidth(width);
    const std::string shown = code_value_text(f.name, r.value);
    if (ImGui::BeginCombo("##bk_code", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char filt[48] = "";
        if (ImGui::IsWindowAppearing()) {
            filt[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##bk_codefilter", "search", filt, sizeof(filt));
        std::vector<int64_t> codes = code_value_list(app, t, f, r.value);
        char* endp = nullptr;
        const long long typed = filt[0] ? std::strtoll(filt, &endp, 10) : 0;
        if (filt[0] && endp && *endp == 0 && typed >= f.min && typed <= f.max() && std::find(codes.begin(), codes.end(), typed) == codes.end())
            codes.insert(codes.begin(), typed);  // a typed number reaches any value of the range
        const std::string q = lower(filt);
        for (int64_t v : codes) {
            const std::string txt = code_value_text(f.name, v);
            if (!contains_ci(txt, q) && std::to_string(v) != filt) continue;
            const std::string item = txt + "##" + std::to_string(v);
            if (ImGui::Selectable(item.c_str(), v == r.value)) {
                r.value = v;
                r.value_chosen = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s = %lld", f.name.c_str(), static_cast<long long>(v));
        }
        ImGui::EndCombo();
    }
}

void pick_nation(App& app, Row& r, float width) {
    const Geo& g = geo(app);
    std::string preview = "Choose a nation";
    if (r.value_chosen) {
        preview = g.nation_name(r.value);
        if (preview.empty()) preview = "Nation " + std::to_string(r.value);
    }
    int64_t sel = r.value_chosen ? r.value : -1;
    if (geo_pick_combo("##bk_nation", "Choose a nation", preview, sel, width, [&](std::vector<GeoOption>& out) {
            for (const GeoNation* n : g.sorted_nations()) out.push_back({n->id, n->name});
        })) {
        r.value = sel;
        r.value_chosen = sel >= 0;
    }
}

void pick_value(App& app, const Table& t, const Field& f, Row& r) {
    const float w = S(240.0f);
    switch (control_of(f)) {
        case Control::Switch: {
            ImGui::SetNextItemWidth(S(110.0f));
            if (ImGui::BeginCombo("##bk_switch", r.value ? "Yes" : "No")) {
                if (ImGui::Selectable("Yes", r.value != 0)) r.value = 1;
                if (ImGui::Selectable("No", r.value == 0)) r.value = 0;
                ImGui::EndCombo();
            }
            break;
        }
        case Control::Date: {
            ImGui::SetNextItemWidth(S(180.0f));
            if (ImGui::InputInt3("##bk_date", r.ymd)) {
                const GameDate d{r.ymd[0], r.ymd[1], r.ymd[2]};
                if (is_real_date(d)) r.value = gregorian_days_from_date(d);
            }
            ImGui::SameLine();
            theme::muted_text("year / month / day");
            break;
        }
        case Control::Nation:
            pick_nation(app, r, w);
            break;
        case Control::Position: {
            int pos = static_cast<int>(r.value);
            if (position_combo("##bk_position", pos, w, f.min < 0 ? "None" : nullptr, 0, -1)) {
                r.value = pos;
                r.value_chosen = true;
            }
            break;
        }
        case Control::Code:
            pick_code(app, t, f, r, w);
            break;
        case Control::Bits: {
            const auto& names = bit_names(f.name);
            ImGui::SetNextItemWidth(w);
            const int bits = std::min<int>(f.depth, static_cast<int>(names.size()));
            r.bit = std::max(0, std::min(r.bit, bits - 1));
            if (ImGui::BeginCombo("##bk_bit", names[static_cast<size_t>(r.bit)] ? names[static_cast<size_t>(r.bit)] : "Unnamed PlayStyle", ImGuiComboFlags_HeightLarge)) {
                for (int b = 0; b < bits; ++b) {
                    if (!names[static_cast<size_t>(b)]) continue;
                    if (ImGui::Selectable(names[static_cast<size_t>(b)], b == r.bit)) r.bit = b;
                }
                ImGui::EndCombo();
            }
            break;
        }
        case Control::Number: {
            if (r.mode == Mode::Add) {
                long long d = r.delta;
                ImGui::SetNextItemWidth(S(130.0f));
                if (ImGui::InputScalar("##bk_delta", ImGuiDataType_S64, &d)) {
                    const long long span = static_cast<long long>(f.max() - f.min);
                    r.delta = clamp64(d, -span, span);
                }
                ImGui::SameLine();
                theme::muted_text("added to each player, kept inside %lld to %lld", static_cast<long long>(f.min), static_cast<long long>(f.max()));
            } else if (f.max() - f.min <= 120) {
                int v = static_cast<int>(r.value);
                ImGui::SetNextItemWidth(w);
                if (ImGui::SliderInt("##bk_slide", &v, static_cast<int>(f.min), static_cast<int>(f.max()))) r.value = v;
            } else {
                long long v = r.value;
                ImGui::SetNextItemWidth(S(130.0f));
                if (ImGui::InputScalar("##bk_num", ImGuiDataType_S64, &v)) r.value = clamp64(v, f.min, f.max());
                ImGui::SameLine();
                theme::muted_text("from %lld to %lld", static_cast<long long>(f.min), static_cast<long long>(f.max()));
            }
            break;
        }
    }
}

// ---------------------------------------------------------------- the page
bool chip(const char* label, bool* on) {
    const bool lit = *on;
    if (lit) ImGui::PushStyleColor(ImGuiCol_Button, theme::accent());
    const bool hit = ImGui::SmallButton(label);
    if (lit) ImGui::PopStyleColor();
    if (hit) *on = !*on;
    return hit;
}

void section_who(App& app) {
    ImGui::SeparatorText("1. Who");
    const int64_t user = app.bridge.state().user_team;
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##bk_search", "Search by name or club", g_f.search, sizeof(g_f.search));
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear filters###bk_clear")) {
        const std::string keep = g_f.search;
        g_f = Filter();
        std::snprintf(g_f.search, sizeof(g_f.search), "%s", keep.c_str());
    }
    // clubs: pick several, each one a chip
    ImGui::SetNextItemWidth(S(240.0f));
    if (team_filter_combo(app, "##bk_team", g_f.club_pick, g_f.club_search, sizeof(g_f.club_search), S(240.0f)) && g_f.club_pick > 0) {
        if (std::find(g_f.clubs.begin(), g_f.clubs.end(), g_f.club_pick) == g_f.clubs.end()) g_f.clubs.push_back(g_f.club_pick);
        g_f.club_pick = 0;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick one or more clubs: their players are the ones listed");
    for (size_t i = 0; i < g_f.clubs.size();) {
        ImGui::SameLine();
        const std::string lbl = app.model.team_name(g_f.clubs[i]) + "  x###bk_club" + std::to_string(g_f.clubs[i]);
        if (ImGui::SmallButton(lbl.c_str())) g_f.clubs.erase(g_f.clubs.begin() + static_cast<long>(i));
        else ++i;
    }
    if (user > 0) {
        chip("My club###bk_chip_mine", &g_f.my_club);
        ImGui::SameLine();
    }
    chip("Free agents###bk_chip_free", &g_f.free_agents);
    ImGui::SameLine();
    chip("Retiring###bk_chip_retiring", &g_f.retiring);
    ImGui::SameLine();
    position_combo("##bk_pos", g_f.pos, S(120.0f), "Any position");
    ImGui::SameLine();
    {
        static const char* const kGenders[] = {"Any gender", "Men", "Women"};
        ImGui::SetNextItemWidth(S(110.0f));
        if (ImGui::BeginCombo("##bk_gender", kGenders[g_f.gender + 1])) {
            for (int k = -1; k <= 1; ++k)
                if (ImGui::Selectable(kGenders[k + 1], g_f.gender == k)) g_f.gender = k;
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    {
        const Geo& g = geo(app);
        std::string preview = "Any nationality";
        if (g_f.nat >= 0) {
            preview = g.nation_name(g_f.nat);
            if (preview.empty()) preview = "Nation " + std::to_string(g_f.nat);
        }
        geo_pick_combo("##bk_nat", "Any nationality", preview, g_f.nat, S(170.0f), [&](std::vector<GeoOption>& out) {
            for (const GeoNation* n : g.sorted_nations()) out.push_back({n->id, n->name});
        });
    }
    form_label_width(S(80.0f));
    range_filter_row("Age", "bk_age", g_f.age.from, g_f.age.to, 99, S(46.0f), "Age, from to (0 = no limit)");
    ImGui::SameLine();
    range_filter_row("Overall", "bk_ovr", g_f.ovr.from, g_f.ovr.to, 99, S(46.0f), "Overall rating, from to (0 = no limit)");
    ImGui::SameLine();
    range_filter_row("Potential", "bk_pot", g_f.pot.from, g_f.pot.to, 99, S(46.0f), "Potential, from to (0 = no limit)");
    form_label_width(S(110.0f));

    refresh_matched(app);
    size_t selected = 0;
    for (int64_t id : g_matched)
        if (!g_off.count(id)) ++selected;
    g_state.matched = g_matched.size();
    g_state.selected = selected;
    ImGui::Text("%zu players match, %zu picked", g_matched.size(), selected);
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick all###bk_pickall")) g_off.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick none###bk_picknone")) g_off.insert(g_matched.begin(), g_matched.end());

    if (ImGui::BeginChild("##bk_list", ImVec2(0, S(190.0f)), ImGuiChildFlags_Borders)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_matched.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const int64_t pid = g_matched[static_cast<size_t>(i)];
                const PlayerRow* p = app.model.player(pid);
                if (!p) continue;
                ImGui::PushID(static_cast<int>(pid));
                bool on = g_off.count(pid) == 0;
                if (ImGui::Checkbox("##bk_tick", &on)) {
                    if (on) g_off.erase(pid);
                    else g_off.insert(pid);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(p->name.c_str());
                ImGui::SameLine(S(260.0f));
                theme::muted_text("%s   %s   overall %d", p->club_name.empty() ? "No club" : p->club_name.c_str(), position_name(p->position), p->overall);
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

void section_what(App& app, const Table& t) {
    ImGui::SeparatorText("2. What to change");
    if (ImGui::Button("Add field###bk_addfield")) ImGui::OpenPopup("##bk_fieldpicker");
    ImGui::SameLine();
    theme::muted_text("any field of the player editor; hover a name for its range");
    if (ImGui::BeginPopup("##bk_fieldpicker")) {
        static char filt[48] = "";
        if (ImGui::IsWindowAppearing()) {
            filt[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(S(320.0f));
        ImGui::InputTextWithHint("##bk_fieldfilter", "Search the fields", filt, sizeof(filt));
        const std::string q = lower(filt);
        if (ImGui::BeginChild("##bk_fieldlist", ImVec2(S(340.0f), S(300.0f)))) {
            std::string last_group;
            for (const std::string& name : list_fields(t)) {
                const std::string title = field_label(name);
                if (!contains_ci(title, q) && !contains_ci(name, q)) continue;
                const std::string g = group_of(name);
                if (q.empty() && g != last_group) {
                    theme::muted_text("%s", g.c_str());
                    last_group = g;
                }
                const bool chosen = find_row(name) != nullptr;
                if (chosen) ImGui::BeginDisabled();
                const std::string item = title + "##" + name;
                if (ImGui::Selectable(item.c_str(), false)) {
                    add_field(app, name);
                    ImGui::CloseCurrentPopup();
                }
                if (chosen) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (const Field* f = t.field(name)) ImGui::SetTooltip("%s\n%s%lld to %lld", name.c_str(), chosen ? "Already added. " : "", static_cast<long long>(f->min), static_cast<long long>(f->max()));
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    for (size_t i = 0; i < g_rows.size();) {
        Row& r = g_rows[i];
        const Field* f = t.field(r.field);
        if (!f) {
            g_rows.erase(g_rows.begin() + static_cast<long>(i));
            continue;
        }
        ImGui::PushID(r.field.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field_label(r.field).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%lld to %lld", r.field.c_str(), static_cast<long long>(f->min), static_cast<long long>(f->max()));
        ImGui::SameLine(S(190.0f));
        const Control c = control_of(*f);
        if (c == Control::Number) {
            if (mode_combo("##bk_mode", r.mode, kModeSet, kModeAdd, Mode::Set, Mode::Add) && r.mode == Mode::Add) r.delta = r.delta == 0 ? 1 : r.delta;
            ImGui::SameLine();
        } else if (c == Control::Bits) {
            if (r.mode != Mode::Give && r.mode != Mode::Take) r.mode = Mode::Give;
            mode_combo("##bk_mode", r.mode, "Give", "Take away", Mode::Give, Mode::Take);
            ImGui::SameLine();
        } else {
            r.mode = Mode::Set;
        }
        pick_value(app, t, *f, r);
        ImGui::SameLine();
        const bool remove = ImGui::SmallButton("Remove###bk_remove");
        ImGui::PopID();
        if (remove) g_rows.erase(g_rows.begin() + static_cast<long>(i));
        else ++i;
    }
    if (g_rows.empty()) theme::muted_text("Nothing added yet.");
}

void section_apply(App& app) {
    ImGui::SeparatorText("3. Apply");
    const std::vector<int64_t> sel = selected_ids();
    g_state.rows = g_rows.size();
    const std::string why = why_not_apply(app, sel);
    g_state.why_not = why;
    build_preview(app, sel);
    for (const std::string& line : g_state.preview) ImGui::TextUnformatted(line.c_str());
    if (sel.size() > kBigRun) {
        char lbl[128];
        std::snprintf(lbl, sizeof(lbl), "Yes, change all %zu players###bk_confirm", sel.size());
        checkbox_right(lbl, &g_confirm_big);
    } else {
        g_confirm_big = false;
    }
    if (!why.empty()) ImGui::BeginDisabled();
    char lbl[96];
    std::snprintf(lbl, sizeof(lbl), "Apply to %zu players###bk_apply", sel.size());
    if (theme::primary_button(lbl)) start_apply(app);
    if (!why.empty()) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why.c_str());
    }
    ImGui::SameLine();
    const bool can_undo = !g_done.empty() && !g_run.active;
    if (!can_undo) ImGui::BeginDisabled();
    std::snprintf(lbl, sizeof(lbl), "Undo the last bulk edit###bk_undo");
    if (ImGui::Button(lbl)) start_undo(app);
    if (!can_undo) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_done.empty() ? "Nothing to undo yet." : "Puts the values back that the last bulk edit replaced (it can be undone while the same save is loaded).");
    if (g_run.active) {
        const float frac = g_state.total ? static_cast<float>(g_state.done) / static_cast<float>(g_state.total) : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(S(260.0f), 0.0f));
        ImGui::SameLine();
        ImGui::Text("%zu of %zu players", g_state.done, g_state.total);
    }
    if (!g_state.result.empty()) ImGui::TextUnformatted(g_state.result.c_str());
    g_state.running = g_run.active;
    g_state.undo_players = g_done.empty() ? 0 : g_done.back().recs.size();
    g_state.undo_what = g_done.empty() ? std::string() : g_done.back().what;
}

void section_actions(App& app) {
    const BridgeState& st = app.bridge.state();
    if (!ImGui::CollapsingHeader("Fitness, form, morale and development (career)###bk_actions_head")) return;
    checkbox_right("Fitness###bk_fit_on", &g_do_fit);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::SliderInt("##bk_fit", &g_fit, 5, 95);
    checkbox_right("Form###bk_form_on", &g_do_form);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::SliderInt("##bk_form", &g_form, 0, 100);
    checkbox_right("Morale###bk_morale_on", &g_do_morale);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::SliderInt("##bk_morale", &g_morale, 0, 100);
    const std::vector<int64_t> sel = selected_ids();
    checkbox_right("Development###bk_dev_on", &g_do_dev);
    if (g_do_dev) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("XP multiplier");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputFloat("##bk_xpmult", &g_xp_mult, 0.0f, 0.0f, "%.1f");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Bonus XP");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("##bk_bonusxp", &g_bonus_xp, 0);
        ImGui::SameLine();
        checkbox_right("No decline###bk_nodecline", &g_no_decline, -1.0f);
    }
    json actions = json::object();
    if (g_do_fit) actions["fitness"] = g_fit;
    if (g_do_form) actions["form"] = g_form;
    if (g_do_morale) actions["morale"] = g_morale;
    if (g_do_dev) actions["development"] = {{"xp_multiplier", g_xp_mult}, {"bonus_xp", g_bonus_xp}, {"no_decline", g_no_decline}};
    json cmd = {{"op", "run"}, {"module", "bulk_edit"},
                {"overrides", {{"scope", {{"playerids", sel}}}, {"filters", json::object()}, {"set", json::object()}, {"actions", actions}, {"confirm_all", false}}}};
    std::string why;
    if (!st.in_cm) why = "needs a loaded career";
    else if (actions.empty()) why = "tick what to set";
    else if (sel.empty()) why = "no player is picked";
    else if (sel.size() > kSendPlayers || cmd.dump().size() > 3900) why = "too many players for one go: pick fewer (about 350 at most)";
    else if ((g_do_fit || g_do_form || g_do_morale) && st.unavailable_reason("form_morale")) why = *st.unavailable_reason("form_morale");
    else if (g_do_dev && st.unavailable_reason("development_xp")) why = "Development: " + *st.unavailable_reason("development_xp");
    else if (app.busy() || !app.mailbox) why = "the game link is busy";
    g_state.actions_why = why;
    if (!why.empty()) ImGui::BeginDisabled();
    // the pending label names only what is ticked ("Bulk edit: development", "Bulk edit: fitness, morale")
    std::string label = "Bulk edit: ";
    {
        std::string names;
        auto add = [&](bool on, const char* n) {
            if (!on) return;
            if (!names.empty()) names += ", ";
            names += n;
        };
        add(g_do_fit, "fitness");
        add(g_do_form, "form");
        add(g_do_morale, "morale");
        add(g_do_dev, "development");
        label += names;
    }
    if (ImGui::Button("Send to the picked players###bk_actions_go")) app.send(cmd, label);
    if (!why.empty()) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why.c_str());
    }
}

void section_preset(App& app) {
    const BridgeState& st = app.bridge.state();
    if (!ImGui::CollapsingHeader("My team preset###bk_preset_head")) return;
    ImGui::TextWrapped("One click for your own club: tight shirts, 60-month contracts, very happy morale, squad roles (Rotation and Prospect), "
                       "transfer offers blocked, and nothing else.");
    std::string why;
    if (!team_preset_apply) why = "not in this build";
    else if (!st.in_cm || st.user_team <= 0) why = "needs a loaded career with your club";
    g_state.preset_enabled = why.empty();
    g_state.preset_line = why.empty() ? std::string("ready") : why;
    if (!why.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("My team preset###bk_preset")) ImGui::OpenPopup("##bk_preset_confirm");
    if (!why.empty()) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", why.c_str());
    }
    if (ImGui::BeginPopupModal("##bk_preset_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Apply the preset to %s?", app.model.team_name(st.user_team).c_str());
        ImGui::TextUnformatted("Every player of the club gets tight shirts, a 60-month contract and very happy morale; squad roles and");
        ImGui::TextUnformatted("blocked transfer offers follow. It can't be undone with one click.");
        if (ImGui::Button("Apply to my team###bk_preset_go")) {
            if (team_preset_apply) team_preset_apply(app, st.user_team);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###bk_preset_cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace

// ---------------------------------------------------------------- the API
const State& state() { return g_state; }

void reset() {
    g_f = Filter();
    g_rows.clear();
    g_matched.clear();
    g_off.clear();
    g_key.clear();
    g_version = ~uint64_t(0);
    g_confirm_big = false;
    g_do_fit = g_do_form = g_do_morale = false;
    g_do_dev = teampreset::kExtraXpDefault;
    g_no_decline = teampreset::kNoDeclineDefault;
    g_bonus_xp = 0;
    g_xp_mult = 2.0f;
    g_run = Run();
    g_done.clear();
    g_state = State();
}

bool field_offered(const std::string& field) { return !excluded_field(field); }
std::string field_group(const std::string& field) { return group_of(field); }
std::string field_title_of(const std::string& field) { return field_label(field); }

std::vector<std::string> field_list(App& app) {
    const Table* t = players_table(app);
    return t ? list_fields(*t) : std::vector<std::string>();
}

Row* row_of(const std::string& field) { return find_row(field); }

bool add_field(App& app, const std::string& field) {
    const Table* t = players_table(app);
    const Field* f = t ? t->field(field) : nullptr;
    if (!f || f->type != FieldType::Int || excluded_field(field) || f->depth <= 0) return false;
    if (find_row(field)) return true;
    Row r;
    r.field = field;
    r.value = clamp64(0, f->min, f->max());
    if (const PlayerRow* p = !g_matched.empty() ? app.model.player(g_matched.front()) : nullptr) r.value = clamp64(app.db.get_int(*t, p->rec, field, r.value), f->min, f->max());
    const Control c = control_of(*f);
    if (c == Control::Nation) r.value_chosen = false;
    if (c == Control::Date) {
        const GameDate d = app.today();
        r.ymd[0] = d.year;
        r.ymd[1] = d.month;
        r.ymd[2] = d.day;
        r.value = gregorian_days_from_date(d);
    }
    if (c == Control::Bits) r.mode = Mode::Give;
    if (c == Control::Number) r.delta = 1;
    g_rows.push_back(r);
    return true;
}

void remove_field(const std::string& field) {
    for (size_t i = 0; i < g_rows.size(); ++i)
        if (g_rows[i].field == field) {
            g_rows.erase(g_rows.begin() + static_cast<long>(i));
            return;
        }
}

void select_all_matching(App& app) {
    refresh_matched(app);
    g_off.clear();
}

void pick_only(App& app, const std::vector<int64_t>& playerids) {
    refresh_matched(app);
    g_off.clear();
    const std::unordered_set<int64_t> want(playerids.begin(), playerids.end());
    for (int64_t id : g_matched)
        if (!want.count(id)) g_off.insert(id);
}

std::vector<int64_t> picked_ids() { return selected_ids(); }

void set_club(App& app, int64_t teamid) {
    g_f.clubs.clear();
    if (teamid > 0) g_f.clubs.push_back(teamid);
    refresh_matched(app);
}

void set_search(const std::string& text) { std::snprintf(g_f.search, sizeof(g_f.search), "%s", text.c_str()); }

int64_t new_value(const Row& r, int64_t current, int64_t min, int64_t max) {
    switch (r.mode) {
        case Mode::Add: return clamp64(current + r.delta, min, max);
        case Mode::Give:
        case Mode::Take: {
            if (r.bit < 0 || r.bit > 61) return current;
            uint64_t raw = static_cast<uint64_t>(current - min);
            const uint64_t bit = uint64_t(1) << r.bit;
            raw = r.mode == Mode::Give ? (raw | bit) : (raw & ~bit);
            return clamp64(static_cast<int64_t>(raw) + min, min, max);
        }
        case Mode::Set:
        default: return clamp64(r.value, min, max);
    }
}

bool start_apply(App& app) {
    refresh_matched(app);
    const std::vector<int64_t> sel = selected_ids();
    g_state.selected = sel.size();
    if (!why_not_apply(app, sel).empty()) return false;
    Run r;
    r.active = true;
    r.pids = sel;
    r.rows = g_rows;
    r.snap = snapshot_fields(g_rows);
    r.what = what_of(g_rows);
    for (const Row& row : g_rows) r.shown.push_back(row.field);
    r.verbose = sel.size() <= kLogReadBack;
    r.key = load_key(app);
    g_run = std::move(r);
    g_state.result.clear();
    g_state.done = 0;
    g_state.total = g_run.pids.size();
    g_state.running = true;
    return true;
}

bool start_undo(App& app) {
    if (g_run.active || g_done.empty()) return false;
    Done d = std::move(g_done.back());
    g_done.pop_back();
    if (d.key != load_key(app)) {
        g_state.result = "Nothing was put back: another save has been loaded since.";
        app.notify(g_state.result, true);
        return false;
    }
    Run r;
    r.active = true;
    r.undoing = true;
    r.back = std::move(d.recs);
    r.shown = d.shown;
    r.verbose = r.back.size() <= kLogReadBack;
    r.key = d.key;
    g_run = std::move(r);
    g_state.result.clear();
    g_state.done = 0;
    g_state.total = g_run.back.size();
    g_state.running = true;
    return true;
}

void run_to_end(App& app, int max_frames) {
    for (int i = 0; i < max_frames && g_run.active; ++i) step_run(app, kPerFrame);
    g_state.running = g_run.active;
    g_state.undo_players = g_done.empty() ? 0 : g_done.back().recs.size();
    g_state.undo_what = g_done.empty() ? std::string() : g_done.back().what;
}

}  // namespace bulkedit

// ---------------------------------------------------------------- the page
void draw_bulk_edit(App& app) {
    using namespace bulkedit;
    // 4.0 (S7): the page template. The staged Apply (preview, big-run confirm, the page's own Undo) stays in the body as section 3; the
    // frame has no Apply of its own, its bar gives the global Undo and Back.
    page::Frame f(app, {"bulk_edit", "Bulk edit", "Change one or more fields for many players at once: pick them, add the fields, apply.", nullptr, true, false});
    if (!app.connected() || !players_table(app)) {
        not_connected_hint();
        return;
    }
    const Table& t = *players_table(app);
    const float prev_label_w = form_label_width();
    form_label_width(S(110.0f));
    step_run(app, kPerFrame);
    // (the first sentence of the old intro is now the page::Frame help above; a template page keeps its grey hints to 100 characters)
    ImGui::TextWrapped("Each edit goes through the same checks as the Player editor.");
    section_who(app);
    section_what(app, t);
    section_apply(app);
    ImGui::Spacing();
    section_actions(app);
    section_preset(app);
    form_label_width(prev_label_w);
}

}  // namespace turbo
