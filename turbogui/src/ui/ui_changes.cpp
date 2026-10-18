// FC 27 LE Turbo GUI - Career > Changes and undo (see ui_changes.h). Plan step 9, sections 5.3 and 5.4.
//
// Where each row comes from (all read-only looks; every write goes through the feature's own function):
//   player edits   App::undo_ (the per-player undo steps) and App::undo_pushes: one row per change, undone with App::undo
//   kit imports    kit_slot_rows / undo_kit_import (ui_kits.h) for your club and the club open on the Kits page, looked at once a second
//   tactic loads   tactic_presets_state().can_undo: the Undo stays a button of the Tactic presets page, so the row opens that page
//   callnames      a new kept callname (App::reapply): no Undo, the row says so
//   other pages    note_irreversible(): called by the page that owns an action with no Undo, after its confirm
// What Turbo puts back at every career load (App::reapply) is listed with "Stop doing this", always behind a confirm. Nothing here
// clears a kept row without that confirm.
#include "ui_changes.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <utility>

#include "app.h"
#include "imgui.h"
#include "table_registry.h"
#include "ui_kits.h"
#include "ui_tactic_presets.h"
#include "ui_theme.h"

namespace turbo {
namespace changes {

namespace {

constexpr size_t kMaxRows = 60;

struct State {
    std::vector<Change> rows;  // newest first
    uint64_t next_seq = 1;
    bool primed = false;       // the first look only records baselines (except the player steps, which are all this session)
    std::map<int64_t, size_t> seen;          // player id -> undo steps mirrored in the rows
    std::map<int64_t, std::string> sig;      // player id -> the newest step as text (to see a step pushed on a full stack)
    uint64_t pushes = 0;
    size_t bulk_seen = 0;
    std::map<std::pair<int64_t, int>, bool> kit_custom;  // (club, slot) -> imported
    double kit_time = -1e9;
    bool tactic_undo = false;
    std::string tactic_line;
    size_t callnames = 0;
};
State g;

// "14:07": the wall clock of the person (a row's time is when Turbo saw the change)
std::string clock_text(const App&) {
    const std::time_t now = std::time(nullptr);
    const std::tm* lt = std::localtime(&now);
    char buf[16] = "now";
    if (lt) std::strftime(buf, sizeof(buf), "%H:%M", lt);
    return buf;
}

std::string player_name(App& app, int64_t pid) {
    const PlayerRow* p = app.db.ready() ? app.model.player(pid) : nullptr;
    return p && !p->name.empty() ? p->name : std::string("a player");
}

std::string step_sig(const App::UndoStep& s) { return s.table + "|" + std::to_string(s.rec) + "|" + s.field + "|" + s.before.to_string(); }

std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

void add(Change c) {
    c.seq = g.next_seq++;
    g.rows.insert(g.rows.begin(), std::move(c));
    if (g.rows.size() > kMaxRows) g.rows.pop_back();
}

// A player's edit text: "Pace for Jamie Vardy", "Overall and 3 more for Jamie Vardy"
std::string edit_text(App& app, int64_t pid, const std::deque<App::UndoStep>& steps, size_t added) {
    size_t fields = 0;
    const size_t from = steps.size() > added ? steps.size() - added : 0;
    for (size_t i = from; i < steps.size(); ++i) fields += 1 + steps[i].also.size();
    const App::UndoStep& first = steps[from];
    std::string what = field_label(first.field);
    if (fields > 1) what += " and " + std::to_string(fields - 1) + " more";
    return what + " for " + player_name(app, pid);
}

void poll_players(App& app) {
    // steps pushed on a full stack leave its size as it was: the total push count says how many were missed
    const uint64_t delta_total = app.undo_pushes >= g.pushes ? app.undo_pushes - g.pushes : 0;
    g.pushes = app.undo_pushes;
    std::map<int64_t, size_t> grew;
    size_t grown_total = 0;
    std::vector<int64_t> full;
    for (auto& kv : app.undo_) {
        const int64_t pid = kv.first;
        const size_t now = kv.second.size();
        const size_t was = g.seen.count(pid) ? g.seen[pid] : 0;
        const std::string sg = now ? step_sig(kv.second.back()) : std::string();
        if (now < was) {
            // taken back somewhere else (the player page): the newest rows of his go
            size_t drop = was - now;
            for (size_t i = 0; i < g.rows.size() && drop;) {
                Change& c = g.rows[i];
                if (c.source != Source::PlayerEdit || c.id != pid) {
                    ++i;
                    continue;
                }
                const size_t take = std::min<size_t>(drop, static_cast<size_t>(c.count));
                c.count -= static_cast<int>(take);
                drop -= take;
                if (c.count <= 0) g.rows.erase(g.rows.begin() + static_cast<long>(i));
                else ++i;
            }
        } else if (now > was) {
            grew[pid] = now - was;
            grown_total += now - was;
        } else if (now == App::kUndoSteps && g.sig.count(pid) && g.sig[pid] != sg) {
            full.push_back(pid);
        }
        g.seen[pid] = now;
        g.sig[pid] = sg;
    }
    if (delta_total > grown_total && full.size() == 1) grew[full[0]] += static_cast<size_t>(delta_total - grown_total);
    for (const auto& kv : grew) {
        const int64_t pid = kv.first;
        const auto it = app.undo_.find(pid);
        if (it == app.undo_.end() || it->second.empty()) continue;
        const size_t added = std::min(kv.second, it->second.size());
        Change c;
        c.when = g.primed ? clock_text(app) : std::string("earlier");
        c.what = edit_text(app, pid, it->second, added);
        c.source = Source::PlayerEdit;
        c.how = How::Undo;
        c.id = pid;
        c.count = static_cast<int>(added);
        add(std::move(c));
    }
    // a bulk change (overall +/-1, an archetype) also keeps an entry for the player page's "Undo bulk": it belongs to the row
    if (app.bulk_undo.size() > g.bulk_seen) {
        for (size_t i = g.bulk_seen; i < app.bulk_undo.size(); ++i)
            for (Change& c : g.rows)
                if (c.source == Source::PlayerEdit && c.id == app.bulk_undo[i].playerid) {
                    ++c.bulk;
                    break;
                }
    }
    g.bulk_seen = app.bulk_undo.size();
    // the stack is capped: the oldest rows lose steps the same way
    for (const auto& kv : g.seen) {
        size_t sum = 0;
        for (const Change& c : g.rows)
            if (c.source == Source::PlayerEdit && c.id == kv.first) sum += static_cast<size_t>(c.count);
        for (size_t i = g.rows.size(); i-- > 0 && sum > kv.second;) {
            Change& c = g.rows[i];
            if (c.source != Source::PlayerEdit || c.id != kv.first) continue;
            const size_t take = std::min<size_t>(sum - kv.second, static_cast<size_t>(c.count));
            c.count -= static_cast<int>(take);
            sum -= take;
            if (c.count <= 0) g.rows.erase(g.rows.begin() + static_cast<long>(i));
        }
    }
}

void poll_kits(App& app, bool force) {
    const double t = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    if (!force && t - g.kit_time < 1.0) return;
    g.kit_time = t;
    std::set<int64_t> clubs;
    const int64_t mine = app.bridge.state().user_team;
    if (mine > 0) clubs.insert(mine);
    if (app.sel_team > 0) clubs.insert(app.sel_team);
    for (int64_t club : clubs) {
        for (const KitSlotRow& r : kit_slot_rows(app, club)) {
            const auto key = std::make_pair(club, static_cast<int>(r.slot));
            // a club seen for the first time (your club once it is known, the club open on the Kits page) only records what is
            // there: an import from an earlier session is not a change of this one, and the global Undo must never offer it
            const auto known = g.kit_custom.find(key);
            const bool was = known != g.kit_custom.end() && known->second;
            const bool first_look = known == g.kit_custom.end();
            g.kit_custom[key] = r.custom;
            if (first_look || r.custom == was) continue;
            if (r.custom) {
                Change c;
                c.when = clock_text(app);
                c.what = std::string(r.label) + " kit for " + app.model.team_name(club);
                c.note = "Restore EA kit puts the game's own kit back for this slot.";
                c.source = Source::KitImport;
                c.how = How::Undo;
                c.id = club;
                c.slot = static_cast<int>(r.slot);
                add(std::move(c));
            } else if (!r.custom) {
                // restored on the Kits page
                g.rows.erase(std::remove_if(g.rows.begin(), g.rows.end(),
                                            [&](const Change& c) { return c.source == Source::KitImport && c.id == club && c.slot == static_cast<int>(r.slot); }),
                             g.rows.end());
            }
        }
    }
}

void poll_tactic() {
    const TacticPresetsState& ts = tactic_presets_state();
    if (ts.can_undo && (!g.tactic_undo || ts.last_line != g.tactic_line)) {
        if (g.primed) {
            g.rows.erase(std::remove_if(g.rows.begin(), g.rows.end(), [](const Change& c) { return c.source == Source::TacticLoad; }), g.rows.end());
            Change c;
            c.when = "now";
            c.what = "Tactic preset loaded";
            c.note = "Press Undo last load on the Tactic presets page. It puts back everything Turbo changed in your tactic.";
            c.source = Source::TacticLoad;
            c.how = How::Open;
            c.page = shell::Page::TacticPresets;
            add(std::move(c));
        }
    } else if (!ts.can_undo && g.tactic_undo) {
        g.rows.erase(std::remove_if(g.rows.begin(), g.rows.end(), [](const Change& c) { return c.source == Source::TacticLoad; }), g.rows.end());
    }
    g.tactic_undo = ts.can_undo;
    g.tactic_line = ts.last_line;
}

void poll_callnames(App& app) {
    const size_t n = app.reapply.callnames.size();
    if (g.primed && n > g.callnames) {
        // the newest callname row cannot be told from the older ones but by its edit time: name the one edited last
        const CallnameEdit* newest = nullptr;
        for (const auto& kv : app.reapply.callnames)
            if (!newest || kv.second.when >= newest->when) newest = &kv.second;
        Change c;
        c.when = clock_text(app);
        c.what = "Commentary name for " + (newest && !newest->player.empty() ? newest->player : std::string("a player"));
        c.note = "Stop doing this (below) only stops Turbo putting it back at the next career load.";
        c.source = Source::Callname;
        c.how = How::Never;
        add(std::move(c));
    }
    g.callnames = n;
}

}  // namespace

const std::vector<Change>& rows() { return g.rows; }
size_t count() { return g.rows.size(); }

void reset_for_tests() { g = State(); }

void poll(App& app, bool force) {
    poll_players(app);
    poll_kits(app, force);
    poll_tactic();
    poll_callnames(app);
    g.primed = true;
}

void note_irreversible(App& app, const std::string& what, const std::string& why) {
    Change c;
    c.when = clock_text(app);
    c.what = what;
    c.note = why.empty() ? std::string("The Undo button cannot bring this back.") : why;
    c.source = Source::Other;
    c.how = How::Never;
    add(std::move(c));
}

// ---------------------------------------------------------------------------------------------------- undo

bool can_undo_row(const App& app, const Change& c) {
    if (c.how != How::Undo) return false;
    if (c.source == Source::PlayerEdit) {
        // the newest row of his goes first: App::undo takes the newest step
        for (const Change& r : g.rows) {
            if (r.source != Source::PlayerEdit || r.id != c.id) continue;
            return r.seq == c.seq && app.undo_count(c.id) >= static_cast<size_t>(c.count);
        }
        return false;
    }
    return c.source == Source::KitImport;
}

const Change* last_undoable(const App& app) {
    for (const Change& c : g.rows)
        if (can_undo_row(app, c)) return &c;
    return nullptr;
}

std::string undo_label(const App& app) {
    const Change* c = last_undoable(app);
    return c ? "Undo: " + clip(c->what, 22) : std::string("Nothing to undo");
}

namespace {
void drop_row(uint64_t seq) {
    g.rows.erase(std::remove_if(g.rows.begin(), g.rows.end(), [&](const Change& r) { return r.seq == seq; }), g.rows.end());
}
}  // namespace

bool undo_row(App& app, uint64_t seq, std::string* msg) {
    const auto found = std::find_if(g.rows.begin(), g.rows.end(), [&](const Change& r) { return r.seq == seq; });
    if (found == g.rows.end()) {
        if (msg) *msg = "That change is no longer in the list.";
        return false;
    }
    const Change c = *found;
    if (!can_undo_row(app, c)) {
        if (msg)
            *msg = c.how == How::Undo ? "Undo his newer change first."
                   : c.how == How::Open ? std::string("The Undo is on the ") + shell::page_label(c.page) + " page."
                                        : "This one cannot be undone.";
        return false;
    }
    if (c.source == Source::PlayerEdit) {
        int done = 0;
        for (int i = 0; i < c.count; ++i) {
            if (!app.undo(c.id)) break;
            ++done;
        }
        // the bulk entries of this change go with it: the player page must not offer to put the same values back again
        for (int b = 0; b < c.bulk; ++b)
            for (size_t i = app.bulk_undo.size(); i-- > 0;)
                if (app.bulk_undo[i].playerid == c.id) {
                    app.bulk_undo.erase(app.bulk_undo.begin() + static_cast<long>(i));
                    break;
                }
        g.bulk_seen = app.bulk_undo.size();
        g.seen[c.id] = app.undo_count(c.id);
        const auto it = app.undo_.find(c.id);
        g.sig[c.id] = it != app.undo_.end() && !it->second.empty() ? step_sig(it->second.back()) : std::string();
        g.pushes = app.undo_pushes;
        drop_row(seq);
        if (!done) {
            if (msg) *msg = "Nothing to undo for this change any more.";
            return false;
        }
        if (msg) *msg = "Undone: " + c.what + ".";
        return true;
    }
    if (c.source == Source::KitImport) {
        std::string why;
        if (!undo_kit_import(app, c.id, static_cast<kits::Slot>(c.slot), &why)) {
            if (msg) *msg = why.empty() ? std::string("The kit could not be put back.") : why;
            return false;
        }
        g.kit_custom[{c.id, c.slot}] = false;
        drop_row(seq);
        if (msg) *msg = "Undone: " + c.what + ".";
        return true;
    }
    return false;
}

bool undo_last(App& app, std::string* msg) {
    const Change* c = last_undoable(app);
    if (!c) {
        if (msg) *msg = "Nothing to undo.";
        return false;
    }
    return undo_row(app, c->seq, msg);
}

// ---------------------------------------------------------------------------------------------------- confirm

// the popup's name: the title shown, the id kept ("Release Jamie Vardy?###ask_release_42")
static std::string popup_name(const Ask& a) { return a.title + "###ask_" + a.id; }

void ask_open(const Ask& a) { ImGui::OpenPopup(popup_name(a).c_str()); }

bool ask_run(const Ask& a) {
    bool yes = false;
    const ImVec2 mid = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(mid, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(popup_name(a).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(S(440.0f));
        ImGui::TextUnformatted(a.body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button((a.ok + "###askyes").c_str())) {
            yes = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###askno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return yes;
}

// ---------------------------------------------------------------------------------------------------- drawing

void draw_status_undo(App& app) {
    poll(app);
    const Change* c = last_undoable(app);
    const std::string label = undo_label(app) + "###changes_status_undo";
    ImGui::BeginDisabled(!c);
    if (ImGui::SmallButton(label.c_str())) {
        std::string msg;
        const bool ok = undo_last(app, &msg);
        app.notify(msg, !ok);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", c ? ("Takes back: " + c->what).c_str() : "Nothing you changed can be taken back from here yet.");
}

namespace {

// What a click on the page asks for; the confirm opens and runs after the lists are drawn (the maps are not changed while they are drawn)
struct Pending {
    enum class Kind { None, ForgetKit, ForgetCallname, ForgetPlayer, Scope, KeepOff } kind = Kind::None;
    Kind target = Kind::None;  // Scope: whose row moves (ForgetKit = a club's kits, ForgetCallname, ForgetPlayer)
    int64_t team = 0;
    std::vector<std::array<int64_t, 3>> kit_rows;  // ForgetKit: teamtechid, kittype, teamkitid of each kit of the club's row
    std::string scope_from, scope_to;
    ReapplyStore::RowKey key{0, std::string()};
    Ask ask;
    bool opened = false;  // the confirm was opened (at the id level of run_pending, where it is drawn)
};
Pending g_pending;

std::string where_text(const App& app, const std::string& scope) {
    switch (scope_kind(scope)) {
        case ScopeKind::Global: return "All careers";
        case ScopeKind::NotAssigned: return "All careers (not chosen yet)";
        case ScopeKind::Career: return app.reapply.rank(scope) > 0 ? "This career only" : "Another career";
        default: return "Not used";
    }
}

std::string plural_n(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// "All careers" / "This career" for one kept row; a click asks first
void scope_buttons(App& app, const std::string& scope, Pending::Kind target, int64_t team, const ReapplyStore::RowKey& key, const std::string& who) {
    const bool identified = !app.reapply.career.empty();
    const std::string here = identified ? app.reapply.edit_scope() : std::string();
    auto ask_move = [&](const std::string& to, const std::string& body) {
        g_pending = Pending();
        g_pending.kind = Pending::Kind::Scope;
        g_pending.target = target;
        g_pending.team = team;
        g_pending.key = key;
        g_pending.scope_from = scope;
        g_pending.scope_to = to;
        g_pending.ask = {"scope", "Change where this applies?", body, "Change"};
    };
    ImGui::BeginDisabled(scope == kScopeGlobal);
    if (ImGui::SmallButton("All careers###chg_scope_all"))
        ask_move(kScopeGlobal, who + ": Turbo will put it back in every career you load. You can change this again here.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!identified || scope == here);
    if (ImGui::SmallButton("This career###chg_scope_here"))
        ask_move(here, who + ": Turbo will put it back only when this career loads. Your other careers keep what the game gives them.");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", identified ? "Only when this career loads" : "Load a career first");
}

// "Stop doing this": asks first, always
void ask_stop(Pending::Kind kind, int64_t team, const ReapplyStore::RowKey& key, const std::string& who) {
    g_pending = Pending();
    g_pending.kind = kind;
    g_pending.team = team;
    g_pending.key = key;
    g_pending.ask = {"stop", "Stop doing this?", who + ": Turbo will no longer put this back when you load your career. What you see now stays until the next load.",
                     "Stop doing this"};
}

// After the lists are drawn: the confirm of the click, and the click's work once it is confirmed
void run_pending(App& app) {
    if (g_pending.kind == Pending::Kind::None) return;
    if (!g_pending.opened) {
        ask_open(g_pending.ask);
        g_pending.opened = true;
        return;
    }
    const bool yes = ask_run(g_pending.ask);
    if (!yes) {
        if (!ImGui::IsPopupOpen(popup_name(g_pending.ask).c_str())) g_pending = Pending();  // cancelled
        return;
    }
    const Pending p = g_pending;
    g_pending = Pending();
    bool done = false;
    switch (p.kind) {
        case Pending::Kind::ForgetKit:
            for (const auto& k : p.kit_rows) done = app.forget_kit_edit(k[0], k[1], k[2]) || done;
            break;
        case Pending::Kind::ForgetCallname: done = app.forget_player_callname(p.key.first); break;
        case Pending::Kind::ForgetPlayer: {
            // this row only (never another career's): its fields one by one
            const auto it = app.reapply.players.find(p.key);
            if (it != app.reapply.players.end()) {
                std::vector<std::string> fields;
                for (const auto& f : it->second.fields) fields.push_back(f.first);
                for (const std::string& f : fields) done = app.forget_kept_player_field(p.key, f) || done;
            }
            break;
        }
        case Pending::Kind::Scope:
            if (p.target == Pending::Kind::ForgetKit) done = app.move_kept_kits(p.team, p.scope_from, p.scope_to);
            else if (p.target == Pending::Kind::ForgetCallname) done = app.move_kept_callname(p.key, p.scope_to);
            else if (p.target == Pending::Kind::ForgetPlayer) done = app.move_kept_player(p.key, p.scope_to);
            break;
        case Pending::Kind::KeepOff: done = app.set_keep_players(false); break;
        default: break;
    }
    if (!done) app.notify("Nothing changed.", true);
    else if (p.kind == Pending::Kind::Scope) app.notify("Done: it now applies to " + where_text(app, p.scope_to) + ".");
    else app.notify("Done: Turbo stops putting this back.");
}

void card_this_session(App& app) {
    ImGui::SeparatorText("This session");
    const auto& list = rows();
    if (list.empty()) {
        theme::muted_text("Nothing changed yet. Edit a player, kit or tactic and it shows up here with an Undo.");
        return;
    }
    if (ImGui::BeginTable("##chgrows", 3, tbl::kData | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, S(64.0f));
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Way back", ImGuiTableColumnFlags_WidthFixed, S(190.0f));
        ImGui::TableHeadersRow();
        uint64_t undo_seq = 0;
        shell::Page open_page = shell::Page::None;
        for (const Change& c : list) {
            ImGui::PushID(static_cast<int>(c.seq));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.when.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.what.c_str());
            if (!c.note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.note.c_str());
            ImGui::TableNextColumn();
            if (c.how == How::Undo) {
                const bool ok = can_undo_row(app, c);
                ImGui::BeginDisabled(!ok);
                if (ImGui::SmallButton("Undo###chg_row_undo")) undo_seq = c.seq;
                ImGui::EndDisabled();
                if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo his newer change first.");
            } else if (c.how == How::Open) {
                if (ImGui::SmallButton("Open the Undo###chg_row_open")) open_page = c.page;
            } else {
                theme::muted_text("Can't be undone");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.note.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        // after the table: the list is not changed while it is drawn
        if (undo_seq) {
            std::string msg;
            const bool ok = undo_row(app, undo_seq, &msg);
            app.notify(msg, !ok);
        }
        if (open_page != shell::Page::None) app.go(open_page);
    }
}

void card_remembered(App& app) {
    ImGui::SeparatorText("Remembered for every career load");
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("The game resets these when a career loads, so Turbo puts them back. Stopping keeps what you see now until the next load.");
    ImGui::PopTextWrapPos();
    if (!app.reapply_status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        theme::muted_text("Last time: %s", app.reapply_status.c_str());
        ImGui::PopTextWrapPos();
    }
    bool keep = app.reapply.keep_players;
    if (ImGui::Checkbox("Keep my player edits###chg_keep_players", &keep)) {
        if (keep) {
            app.set_keep_players(true);
        } else {
            g_pending = Pending();
            g_pending.kind = Pending::Kind::KeepOff;
            g_pending.ask = {"keepoff", "Stop putting player edits back?", "Your edited skills stay in the list below, but Turbo will not write them again when a career loads.",
                             "Stop"};
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skills, overall and the like that you edit in a career are written again at every load");
    if (app.reapply.empty() && app.team_names.entries.empty()) {
        theme::muted_text("Nothing is remembered yet.");
        run_pending(app);
        return;
    }
    // a club's kit rows of one scope: one line
    struct ClubKits {
        int64_t team = 0;
        std::string scope, name;
        std::vector<const KitEdit*> rows;
    };
    std::map<std::pair<int64_t, std::string>, ClubKits> clubs;
    for (const auto& kv : app.reapply.kits) {
        const KitEdit& k = kv.second;
        ClubKits& c = clubs[{k.teamtechid, k.scope}];
        c.team = k.teamtechid;
        c.scope = k.scope;
        if (c.name.empty()) c.name = k.team.empty() ? app.model.team_name(k.teamtechid) : k.team;
        c.rows.push_back(&k);
    }
    if (ImGui::BeginTable("##chgkept", 3, tbl::kData | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Applies to", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(300.0f));
        ImGui::TableHeadersRow();
        for (const auto& kv : clubs) {
            const ClubKits& c = kv.second;
            ImGui::PushID(("k" + std::to_string(c.team) + "|" + c.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s: kit colours and settings (%s)", c.name.c_str(), plural_n(c.rows.size(), "kit", "kits").c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Club number %lld", static_cast<long long>(c.team));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(where_text(app, c.scope).c_str());
            ImGui::TableNextColumn();
            // a kit is forgotten through the row in effect here: only when it is this very row (a higher-ranked row of the same kit would go instead)
            bool in_effect = app.reapply.rank(c.scope) > 0;
            for (const KitEdit* k : c.rows) in_effect = in_effect && app.reapply.kit(k->teamtechid, k->kittype, k->teamkitid) == k;
            scope_buttons(app, c.scope, Pending::Kind::ForgetKit, c.team, ReapplyStore::RowKey{0, std::string()}, c.name + " kits");
            ImGui::SameLine();
            ImGui::BeginDisabled(!in_effect);
            if (ImGui::SmallButton("Stop doing this###chg_stop_kit")) {
                ask_stop(Pending::Kind::ForgetKit, c.team, ReapplyStore::RowKey{0, std::string()}, c.name + " kits");
                for (const KitEdit* k : c.rows) g_pending.kit_rows.push_back({k->teamtechid, k->kittype, k->teamkitid});
            }
            ImGui::EndDisabled();
            if (!in_effect && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("This one belongs to another career, or a row for every career comes first.");
            ImGui::PopID();
        }
        for (const auto& e : app.team_names.entries) {
            ImGui::PushID(("n" + std::to_string(e.teamid) + "|" + e.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Club name: %s", e.text[tnames::Full].c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(where_text(app, e.scope).c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Open Club name###chg_open_name")) app.go(shell::Page::Identity);
            ImGui::PopID();
        }
        for (const auto& kv : app.reapply.callnames) {
            const CallnameEdit& cn = kv.second;
            ImGui::PushID(("c" + std::to_string(cn.playerid) + "|" + cn.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Commentary name for %s", cn.player.empty() ? "a player" : cn.player.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(where_text(app, cn.scope).c_str());
            ImGui::TableNextColumn();
            const std::string who = "The commentary name for " + (cn.player.empty() ? std::string("a player") : cn.player);
            scope_buttons(app, cn.scope, Pending::Kind::ForgetCallname, 0, kv.first, who);
            ImGui::SameLine();
            const bool in_effect = app.reapply.callname(cn.playerid) == &cn;
            ImGui::BeginDisabled(!in_effect);
            if (ImGui::SmallButton("Stop doing this###chg_stop_callname")) ask_stop(Pending::Kind::ForgetCallname, 0, kv.first, who);
            ImGui::EndDisabled();
            if (!in_effect && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("This one belongs to another career, or a row for every career comes first.");
            ImGui::PopID();
        }
        for (const auto& kv : app.reapply.players) {
            const PlayerEdit& e = kv.second;
            ImGui::PushID(("p" + std::to_string(e.playerid) + "|" + e.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const PlayerRow* pr = app.db.ready() ? app.model.player(e.playerid) : nullptr;
            const std::string who = pr ? pr->name : (e.player.empty() ? std::string("a player") : e.player);
            ImGui::Text("Skills of %s (%s)", who.c_str(), plural_n(e.fields.size(), "field", "fields").c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player number %lld", static_cast<long long>(e.playerid));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(where_text(app, e.scope).c_str());
            ImGui::TableNextColumn();
            scope_buttons(app, e.scope, Pending::Kind::ForgetPlayer, 0, kv.first, "The skills of " + who);
            ImGui::SameLine();
            const bool here = app.reapply.rank(e.scope) > 0;
            ImGui::BeginDisabled(!here);
            if (ImGui::SmallButton("Stop doing this###chg_stop_player")) ask_stop(Pending::Kind::ForgetPlayer, 0, kv.first, "The skills of " + who);
            ImGui::EndDisabled();
            if (!here && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This one belongs to another career.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    run_pending(app);
}

void card_elsewhere(App& app) {
    ImGui::SeparatorText("Undo buttons on other pages");
    struct Row {
        const char* what;
        const char* button;
        shell::Page page;
    };
    static const Row kRows[] = {
        {"Tactic preset load", "Undo last load", shell::Page::TacticPresets},
        {"Kit for a club", "Restore EA kit", shell::Page::Kits},
        {"League table edit", "Undo", shell::Page::Standings},
        {"Season update pack", "Undo this pack", shell::Page::CMTracker},
        {"Hair and beard", "Revert", shell::Page::PlayerEdit},
        {"Crest from another club", "Remove custom crest", shell::Page::Identity},
        {"Game slider presets", "Discard staged", shell::Page::GameplayPresets},
    };
    if (ImGui::BeginTable("##chgelsewhere", 3, tbl::kData | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, S(170.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(110.0f));
        ImGui::TableHeadersRow();
        for (const Row& r : kRows) {
            ImGui::PushID(r.what);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.what);
            ImGui::TableNextColumn();
            theme::muted_text("%s", r.button);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Open page###chg_open_page")) app.go(r.page);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void card_never() {
    ImGui::SeparatorText("Can't be undone");
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("The Undo button cannot bring these back:");
    ImGui::PopTextWrapPos();
    static const char* const kItems[] = {
        "Releasing a player, ending a loan, transfer and loan lists, moving a player to another club",
        "Deleting, copying or creating a player, importing a player file over a player",
        "Club name, team colours, stadium colours, a manager's head",
        "Squad roles, growth buttons, manager rules",
        "Kit colours and fonts, a commentary name for a player (Stop doing this only stops Turbo putting them back)",
    };
    for (const char* t : kItems) {
        ImGui::Bullet();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(t);
        ImGui::PopTextWrapPos();
    }
}

void editor_changes(App& app, void*) {
    poll(app);
    theme::muted_text("What did I change?");
    card_this_session(app);
    ImGui::Spacing();
    card_remembered(app);
    ImGui::Spacing();
    card_elsewhere(app);
    ImGui::Spacing();
    card_never();
}

}  // namespace

void draw_changes_page(App& app) {
    poll(app);
    const Change* c = last_undoable(app);
    static std::string label;
    label = undo_label(app) + "###changes_undo";
    shell::page::Primary primary;
    primary.label = label.c_str();
    primary.enabled = c != nullptr;
    shell::page::frame(app, nullptr, editor_changes, nullptr, &primary);
    if (primary.clicked) {
        std::string msg;
        const bool ok = undo_last(app, &msg);
        app.notify(msg, !ok);
    }
}

}  // namespace changes
}  // namespace turbo
