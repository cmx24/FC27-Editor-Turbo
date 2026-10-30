// FC 27 LE Turbo GUI - the Player page (see ui_player_page.h). Cards instead of ten tabs, names and pictures instead of IDs (an ID
// shows on hover only), the moves and the player files in one More section. Everything is drawn by the editors that already exist.
#include "ui_player_page.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "ui_faces.h"
#include "core/field_labels.h"
#include "core/legacy.h"
#include "core/model.h"
#include "imgui.h"
#include "move_rules.h"
#include "playstyles.h"
#include "table_registry.h"
#include "ui_bodytypes.h"
#include "ui_club_search.h"
#include "ui_faces3d_gate.h"
#include "ui_images.h"
#include "ui_visual_picker.h"
#include "ui_names.h"
#include "ui_presets.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace {

using nlohmann::json;

PlayerPageState g_state;

// the list's own state (one page, one list)
char g_search[64] = "";
bool g_my_club = false, g_free = false;
int g_pos = -1;
std::vector<int64_t> g_ids;     // the players the list shows, in order (ids, not pointers: the model may rebuild)
std::string g_list_key;
uint64_t g_list_version = ~uint64_t(0);

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool digits_only(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// ---------------------------------------------------------------- the list
void rebuild_list(App& app, const PlayerPageFilter& f, int64_t user_team) {
    std::vector<const PlayerRow*> rows;
    for (const PlayerRow& p : app.model.players())
        if (player_page_matches(p, f, user_team)) rows.push_back(&p);
    // a narrowed list (your club, free agents) reads best by rating; the whole game by name
    const bool by_rating = f.my_club || f.free_agents;
    std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
        if (by_rating && a->overall != b->overall) return a->overall > b->overall;
        return lower_ascii(a->name) < lower_ascii(b->name);
    });
    g_ids.clear();
    g_ids.reserve(rows.size());
    for (const PlayerRow* r : rows) g_ids.push_back(r->playerid);
    // the pictures the list will want first (the old list queued every row; a few hundred is enough for the first screens)
    app.list_player_ids.assign(g_ids.begin(), g_ids.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(g_ids.size(), 300)));
}

// A filter chip: a small toggle button that lights up when on
bool chip(const char* label, bool* on) {
    const bool lit = *on;
    if (lit) ImGui::PushStyleColor(ImGuiCol_Button, theme::accent());
    const bool hit = ImGui::SmallButton(label);
    if (lit) ImGui::PopStyleColor();
    if (hit) *on = !*on;
    return hit;
}

void select_panel(App& app, void*) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ppsearch", "Search by name or club", g_search, sizeof(g_search));
    const int64_t user = app.bridge.state().user_team;
    const bool had_mine = g_my_club;
    if (user > 0) {
        chip("My club###pp_chip_mine", &g_my_club);
        ImGui::SameLine();
    }
    chip("Free agents###pp_chip_free", &g_free);
    if (g_my_club && !had_mine) g_free = false;
    ImGui::SameLine();
    position_combo("##pppos", g_pos, S(110.0f), "Any position");

    PlayerPageFilter f;
    f.text = lower_ascii(g_search);
    f.my_club = g_my_club && user > 0;
    f.free_agents = g_free && !f.my_club;
    f.position = g_pos;
    const std::string key = f.text + "|" + (f.my_club ? "m" : "-") + (f.free_agents ? "f" : "-") + "|" + std::to_string(f.position) + "|" +
                            std::to_string(user);
    if (key != g_list_key || app.model.version() != g_list_version) {
        rebuild_list(app, f, user);
        g_list_key = key;
        g_list_version = app.model.version();
    }
    g_state.listed = g_ids.size();
    theme::muted_text("%zu players", g_ids.size());

    // "##players" is the registered ID of the player list (table_registry.h); this table lives in another window, so its saved layout is its own
    if (ImGui::BeginTable("##players", 3, tbl::kData | ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);  // 2.22 G-resize: a header row (drag its edges to resize the columns; click Name / Pos / OVR is not sorted)
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, S(34.0f));
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed, S(30.0f));
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_ids.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PlayerRow* p = app.model.player(g_ids[static_cast<size_t>(i)]);
                if (!p) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char lbl[200];
                std::snprintf(lbl, sizeof(lbl), "%s###pp%lld", p->name.c_str(), static_cast<long long>(p->playerid));
                if (ImGui::Selectable(lbl, app.sel_player == p->playerid, ImGuiSelectableFlags_SpanAllColumns)) app.sel_player = p->playerid;
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s%s\nPlayer ID %lld", p->club_name.empty() ? "No club" : p->club_name.c_str(), p->on_loan ? " (on loan)" : "",
                                      static_cast<long long>(p->playerid));
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(position_name(p->position));
                ImGui::TableNextColumn();
                ImGui::Text("%d", p->overall);
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- cards
void card_begin(const char* id, const char* title, const char* hint = nullptr) {
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted(title);
    if (hint) {
        ImGui::SameLine();
        theme::muted_text("%s", hint);
    }
    ImGui::Separator();
}
void card_end() {
    ImGui::EndChild();
    ImGui::Spacing();
}

// A fold-out inside a card; true while open (the caller draws the content and calls ImGui::TreePop)
bool fold(const char* label) { return ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth); }

const std::vector<std::pair<const char*, std::vector<const char*>>>& skill_groups() {
    static const std::vector<std::pair<const char*, std::vector<const char*>>> g = {
        {"Pace###pp_sk_pace", {"acceleration", "sprintspeed"}},
        {"Shooting###pp_sk_shoot", {"positioning", "finishing", "shotpower", "longshots", "volleys", "penalties"}},
        {"Passing###pp_sk_pass", {"vision", "crossing", "freekickaccuracy", "shortpassing", "longpassing", "curve"}},
        {"Dribbling###pp_sk_drib", {"agility", "balance", "reactions", "ballcontrol", "dribbling", "composure"}},
        {"Defending###pp_sk_def", {"interceptions", "headingaccuracy", "defensiveawareness", "marking", "standingtackle", "slidingtackle"}},
        {"Physical###pp_sk_phys", {"jumping", "stamina", "strength", "aggression"}},
        {"Goalkeeping###pp_sk_gk", {"gkdiving", "gkhandling", "gkkicking", "gkpositioning", "gkreflexes"}},
    };
    return g;
}

void playstyle_chips(App& app, const Table& t, uint64_t rec, const char* field, const std::vector<const char*>& names) {
    const Field* f = t.field(field);
    if (!f) return;
    Value v;
    if (!app.db.get(t, rec, *f, v)) return;
    ImGui::PushID(field);
    const int bits = std::min(f->depth, 62);
    if (ImGui::BeginTable("##bits", 3, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
        for (int b = 0; b < bits && b < static_cast<int>(names.size()); ++b) {
            if (!names[static_cast<size_t>(b)]) continue;  // an unnamed bit stays out of the main view
            ImGui::TableNextColumn();
            bool on = (static_cast<uint64_t>(v.i - f->min) >> b) & 1;
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "%s##%d", names[static_cast<size_t>(b)], b);
            if (ImGui::Checkbox(lbl, &on)) {
                uint64_t raw = static_cast<uint64_t>(v.i - f->min);
                raw = on ? (raw | (uint64_t(1) << b)) : (raw & ~(uint64_t(1) << b));
                app.edit(t, rec, *f, Value::of_int(static_cast<int64_t>(raw) + f->min));
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

// ---- Looks
void looks_card(App& app, const Table& t, const PlayerRow& p) {
    card_begin("##pp_looks", "Looks");
    const int64_t head_class = app.db.get_int(t, p.rec, "headclasscode", -1);
    g_state.face_line = head_class == 0 ? "Real face" : "Generic face";
    draw_legacy_picture(app, legacy_path::player_miniface(p.playerid), S(72.0f), true);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(g_state.face_line.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Head %lld", static_cast<long long>(app.db.get_int(t, p.rec, "headassetid", 0)));
    if (theme::primary_button("Choose a real face...###pp_face")) ImGui::OpenPopup("Choose a real face");
    ImGui::EndGroup();
    real_face_picker(app, p.playerid);  // the modal, in the same id scope as the button above
    // the live card: since 2.16 a real face shows his new hair and beard with no restart
    if (fold("Hair and beard###pp_hair")) {
        faces3d_gate_section(app, t, p);
        ImGui::TreePop();
    }
    if (fold("Tattoos, hairstyle, boots and gloves###pp_gear")) {
        tattoo_editor(app, t, p.rec);
        item_galleries(app, t, p.rec);
        vp::inline_appearance(app, t, p.rec);  // 2.22 G-pickers: eyebrows, colours, generic head with their pictures
        ImGui::TreePop();
    }
    if (fold("The small picture in menus###pp_mini")) {
        MinifaceTarget mt;
        mt.path = legacy_path::player_miniface(p.playerid);
        mt.headassetid = app.db.get_int(t, p.rec, "headassetid", 0);
        mt.id = p.playerid;
        mt.teamid = p.club;
        mt.table = &t;
        mt.rec = p.rec;
        miniface_editor(app, mt);
        ImGui::TreePop();
    }
    if (fold("Body type###pp_body")) {
        bodytype_gallery_button(app, t, p.rec, false);
        ImGui::TreePop();
    }
    card_end();
}

// ---- Skills
void skills_card(App& app, const Table& t, const PlayerRow& p) {
    card_begin("##pp_skills", "Skills", "moving a skill moves his overall");
    const int64_t stored = app.db.get_int(t, p.rec, "overallrating", 0);
    const Field* of = t.field("overallrating");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Overall %lld", static_cast<long long>(stored));
    for (int delta : {-1, 1}) {
        ImGui::SameLine();
        const bool in_range = of && stored + delta >= std::max<int64_t>(1, of->min) && stored + delta <= std::min<int64_t>(99, of->max());
        if (!in_range) ImGui::BeginDisabled();
        if (ImGui::SmallButton(delta > 0 ? "+1###pp_ovr_up" : "-1###pp_ovr_down")) {
            if (!app.set_overall(t, p.rec, stored + delta)) app.notify("The overall could not be changed this way.", true);
        }
        if (!in_range) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Moves the skills that count most for his position");
    }
    ImGui::SameLine();
    if (const Field* pf = t.field("potential")) {
        ImGui::AlignTextToFramePadding();
        theme::muted_text("Potential");
        ImGui::SameLine();
        field_editor(app, t, p.rec, *pf, nullptr, S(70.0f));
    }
    const bool keeper = p.positions[0] == 0;
    for (const auto& g : skill_groups()) {
        if (std::string(g.first).find("pp_sk_gk") != std::string::npos && !keeper) continue;
        std::vector<const Field*> present;
        long long sum = 0;
        for (const char* fn : g.second)
            if (const Field* f = t.field(fn)) {
                present.push_back(f);
                sum += app.db.get_int(t, p.rec, fn, 0);
            }
        if (present.empty()) continue;
        // "Pace  81": the group name and its average on the fold-out line; the label before ### is what the player reads
        char head[96];
        const char* id = std::strstr(g.first, "###");
        std::snprintf(head, sizeof(head), "%.*s  %lld%s", static_cast<int>(id ? id - g.first : 0), g.first,
                      sum / static_cast<long long>(present.size()), id ? id : "");
        if (fold(head)) {
            if (ImGui::BeginTable("##sl", 1, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
                for (const Field* f : present) {
                    ImGui::TableNextColumn();
                    slider_editor_ex(app, t, p.rec, *f, field_label(f->name).c_str(), S(150.0f), -FLT_MIN, S(58.0f));
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }
    if (fold("PlayStyles and traits###pp_styles")) {
        theme::muted_text("PlayStyles");
        playstyle_chips(app, t, p.rec, "trait1", playstyle1_names());
        theme::muted_text("PlayStyles+");
        playstyle_chips(app, t, p.rec, "icontrait1", playstyle1_names());
        theme::muted_text("Traits");
        playstyle_chips(app, t, p.rec, "trait2", playstyle2_names());
        theme::muted_text("Traits+");
        playstyle_chips(app, t, p.rec, "icontrait2", playstyle2_names());
        ImGui::TreePop();
    }
    card_end();
}

// ---- Position & style
void position_card(App& app, const Table& t, const PlayerRow& p) {
    card_begin("##pp_position", "Position & style");
    auto position_row = [&](int k) {
        const Field* f = t.field("preferredposition" + std::to_string(k));
        if (!f) return;
        Value v;
        if (!app.db.get(t, p.rec, *f, v)) return;
        ImGui::PushID(k);
        form_label(k == 1 ? "Main position" : "Another position");
        int cur = static_cast<int>(v.i);
        if (position_combo("##pos", cur, S(190.0f), f->min <= -1 ? "None" : nullptr, static_cast<int>(std::max<int64_t>(0, f->min)),
                           static_cast<int>(f->max())))
            app.edit(t, p.rec, *f, Value::of_int(cur));
        ImGui::PopID();
    };
    position_row(1);
    if (fold("Other positions###pp_otherpos")) {
        for (int k = 2; k <= 7; ++k) position_row(k);
        ImGui::TreePop();
    }
    field_grid(app, t, p.rec, {"preferredfoot", "weakfootabilitytypecode", "skillmoves", "attackingworkrate", "defensiveworkrate"}, "##ppstyle", 2);
    if (fold("Build and nation###pp_build")) {
        field_grid(app, t, p.rec, {"height", "weight", "nationality", "internationalrep"}, "##ppbuild", 2);
        if (const Field* f = t.field("birthdate")) date_field_editor(app, t, p.rec, *f, "Date of birth");
        ImGui::TreePop();
    }
    card_end();
}

// ---- Contract & club
void contract_card(App& app, const Table& t, const PlayerRow& p) {
    card_begin("##pp_contract", "Contract & club");
    field_grid(app, t, p.rec, {"contractvaliduntil", "wage", "releaseclause", "isretiring"}, "##ppcontract", 2);
    if (const Table* lt = app.db.table("teamplayerlinks")) {
        for (const auto& l : app.model.links_of_player(p.playerid)) {
            ImGui::PushID(static_cast<int>(l.rec & 0x7FFFFFFF));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s", app.model.team_name(l.teamid).c_str(), app.model.is_national_team(l.teamid) ? " (national team)" : "");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Club ID %lld", static_cast<long long>(l.teamid));
            if (const Field* jf = lt->field("jerseynumber")) {
                ImGui::SameLine();
                field_editor(app, *lt, l.rec, *jf, "Shirt number", S(80.0f));
            }
            ImGui::PopID();
        }
    }
    const BridgeState& st = app.bridge.state();
    if (st.in_cm && st.user_team > 0 && p.club == st.user_team) {
        ImGui::PushID("pp_role");
        static int role = 2;
        const char* roles[] = {"Crucial", "Important", "Rotation", "Sporadic", "Prospect"};
        form_label("Squad role", "Shown on the Squad screen after you open it again; the game works every role out again at the start of a season");
        ImGui::SetNextItemWidth(S(150.0f));
        ImGui::Combo("##role", &role, [](void* d, int i) { return static_cast<const char**>(d)[i]; }, roles, 5);
        if (role < 0 || role > 4) role = 2;
        const bool off = app.busy() || !app.mailbox;
        if (off) ImGui::BeginDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Set role###pp_setrole"))
            app.send({{"op", "run"}, {"module", "squad_role"}, {"overrides", {{"playerid", p.playerid}, {"role", role + 1}}}}, "Squad role " + p.name);
        if (off) ImGui::EndDisabled();
        ImGui::PopID();
    }
    card_end();
}

// ---- Name & commentary
void name_card(App& app, const Table& t, const PlayerRow& p) {
    card_begin("##pp_name", "Name & commentary");
    if (fold("Name shown in game###pp_names")) {
        names_editor(app, t, p);
        ImGui::TreePop();
    }
    if (fold("What the commentator says###pp_callname")) {
        callname_editor(app, t, p);
        ImGui::TreePop();
    }
    card_end();
}

// ---------------------------------------------------------------- More: moves, lists, files
move_rules::Facts move_facts(App& app, const PlayerRow& p) {
    move_rules::Facts f;
    f.user_team = app.bridge.state().user_team;
    f.club = p.club;
    if (const Table* lt = app.db.table("playerloans")) {
        if (uint64_t rec = app.db.find(*lt, "playerid", p.playerid)) {
            f.on_loan = true;
            f.loaned_from = app.db.get_int(*lt, rec, "teamidloanedfrom", 0);
        }
    } else {
        f.loans_known = false;
    }
    if (p.club > 0) {
        const auto squad = app.model.links_of_team(p.club);
        f.squad = static_cast<int>(squad.size());
        if (p.positions[0] == 0) {
            f.last_keeper = true;
            for (const auto& l : squad) {
                const PlayerRow* m = l.playerid != p.playerid ? app.model.player(l.playerid) : nullptr;
                if (m && m->positions[0] == 0) {
                    f.last_keeper = false;
                    break;
                }
            }
        }
    }
    return f;
}

void send_move(App& app, const char* action, int64_t pid, const char* label, const json& extra = json::object()) {
    json a = {{"action", action}, {"playerid", pid}};
    for (auto it = extra.begin(); it != extra.end(); ++it) a[it.key()] = it.value();
    app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, label);
}

// A button for a move: greyed out only when this game build cannot do it at all; when it cannot run for THIS player (`why`) the
// label dims, hovering says why and a click shows the reason
bool move_button(App& app, const char* label, const char* key, const std::string& why, const char* tip) {
    if (const std::string* missing = app.bridge.state().unavailable_reason(key)) {
        ImGui::BeginDisabled();
        ImGui::Button(label);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", missing->c_str());
        return false;
    }
    if (!why.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::Button(label);
    if (!why.empty()) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not possible for him: %s", why.c_str());
        if (clicked) app.notify(std::string(label) + ": " + why, true);
        return false;
    }
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return clicked;
}

const char* const kConfirmPopup = "Are you sure?###pp_confirm";
enum class Confirm { None, Release, EndLoan, Delete };
Confirm g_confirm = Confirm::None;
int g_to_team = 0, g_fee = 0, g_wage = 0, g_months = 60, g_loan_months = 12;

void more_section(App& app, const Table& t, const PlayerRow& p) {
    (void)t;
    const bool cm = app.bridge.state().in_cm;
    const move_rules::Facts facts = move_facts(app, p);
    const char* nobody_undoes = "Turbo's Undo cannot bring this back.";
    if (!cm) ImGui::BeginDisabled();
    theme::muted_text("Moves and lists. They run in the game and cannot be undone from here.");
    if (ImGui::Button("Move to another club...###pp_move")) ImGui::OpenPopup("Move to another club###pp_movedlg");
    ImGui::SameLine();
    if (move_button(app, "Release...###pp_release", "move_release", move_rules::why_not("release", facts), nobody_undoes)) {
        g_confirm = Confirm::Release;
        ImGui::OpenPopup(kConfirmPopup);
    }
    ImGui::SameLine();
    if (move_button(app, "End loan...###pp_endloan", "move_terminate_loan", move_rules::why_not("terminate_loan", facts), nobody_undoes))
    {
        g_confirm = Confirm::EndLoan;
        ImGui::OpenPopup(kConfirmPopup);
    }
    struct B { const char* label; const char* action; const char* key; const char* tip; };
    const B lists[] = {
        {"Transfer list###pp_tlist", "transfer_list", "move_transfer_list", "Adds him to your club's transfer list; other clubs then make offers"},
        {"Loan list###pp_llist", "loan_list", "move_loan_list", "Adds him to your club's loan list"},
        {"Remove from lists###pp_unlist", "unlist", "move_unlist", "Takes him off the transfer list and the loan list"},
    };
    for (const B& b : lists) {
        if (move_button(app, b.label, b.key, move_rules::why_not(b.action, facts), b.tip)) send_move(app, b.action, p.playerid, b.label);
        ImGui::SameLine();
    }
    if (move_button(app, "Delete player...###pp_delete", "delete_players", move_rules::why_not("delete", facts), nobody_undoes)) {
        g_confirm = Confirm::Delete;
        ImGui::OpenPopup(kConfirmPopup);
    }
    if (!cm) {
        ImGui::EndDisabled();
        theme::muted_text("Open a career first: moves only work inside one.");
    }

    // the move dialog: club, fee, wage, contract
    if (ImGui::BeginPopupModal("Move to another club###pp_movedlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        club_search_box(app, "To club###pp_toclub", g_to_team, S(220.0f), "Club name");
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Fee###pp_fee", &g_fee, 0);
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Wage###pp_wage", &g_wage, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 keeps his current wage");
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Contract months###pp_months", &g_months, 0);
        const bool exists = app.model.team(g_to_team) != nullptr;
        const bool national = exists && app.model.is_national_team(g_to_team);
        const int to_squad = exists ? static_cast<int>(app.model.links_of_team(g_to_team).size()) : -1;
        const std::string why_t = move_rules::why_not_to("transfer", facts, g_to_team, exists, national, to_squad);
        const std::string why_l = move_rules::why_not_to("loan", facts, g_to_team, exists, national, to_squad);
        if (facts.user_team > 0 && (g_to_team == facts.user_team || facts.club == facts.user_team))
        {
            ImGui::PushTextWrapPos(S(380.0f));  // a wrapped line in a self-sizing dialog needs a width of its own
            ImGui::TextUnformatted(move_rules::own_club_note(app.bridge.state().game_moves));
            ImGui::PopTextWrapPos();
        }
        theme::warn_text("A move cannot be undone from Turbo.");
        if (move_button(app, "Transfer###pp_dotransfer", "move_transfer", why_t, nullptr)) {
            send_move(app, "transfer", p.playerid, "Transfer",
                      {{"to_teamid", g_to_team}, {"fee", g_fee}, {"wage", g_wage}, {"months", g_months}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(80.0f));
        ImGui::InputInt("##pp_loanm", &g_loan_months, 0);
        ImGui::SameLine();
        if (move_button(app, "Loan (months)###pp_doloan", "move_loan", why_l, nullptr)) {
            send_move(app, "loan", p.playerid, "Loan", {{"to_teamid", g_to_team}, {"months", g_loan_months}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###pp_movecancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // one confirmation for the three moves nothing can bring back
    if (ImGui::BeginPopupModal(kConfirmPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* verb = g_confirm == Confirm::Release ? "Release" : g_confirm == Confirm::EndLoan ? "End the loan of" : "Delete";
        const char* what = g_confirm == Confirm::Release   ? "He becomes a free agent."
                           : g_confirm == Confirm::EndLoan ? "He goes back to the club that lent him."
                                                           : "Turbo releases him first, then removes him and his loan records.";
        char title[200];
        std::snprintf(title, sizeof(title), "%s %s?", verb, p.name.c_str());
        g_state.confirm_text = std::string(title) + " " + what + " The Undo button cannot bring this back.";
        ImGui::TextUnformatted(title);
        ImGui::PushTextWrapPos(S(380.0f));
        ImGui::TextUnformatted(what);
        ImGui::PopTextWrapPos();
        theme::warn_text("The Undo button cannot bring this back.");
        char go[48];
        std::snprintf(go, sizeof(go), "%s###pp_confirm_go", g_confirm == Confirm::Release ? "Release" : g_confirm == Confirm::EndLoan ? "End loan" : "Delete");
        if (ImGui::Button(go)) {
            if (g_confirm == Confirm::Release) send_move(app, "release", p.playerid, "Release");
            else if (g_confirm == Confirm::EndLoan) send_move(app, "terminate_loan", p.playerid, "End loan");
            else send_move(app, "delete", p.playerid, "Delete player", {{"confirm", true}});
            g_confirm = Confirm::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###pp_confirm_no")) {
            g_confirm = Confirm::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    player_preset_buttons(app, p);  // Export, Import, Clone, Create player: the buttons and their dialogs
}

// ---------------------------------------------------------------- the editor side
void editor_panel(App& app, void*) {
    g_state.confirm_text.clear();
    g_state.more_open = false;
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p) {
        g_state.playerid = 0;
        theme::muted_text("Pick a player on the left.");
        return;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game data changed (another career was loaded?). Use Refresh at the top.");
        return;
    }
    g_state.playerid = p->playerid;
    char head[200];
    std::snprintf(head, sizeof(head), "%s, %s, overall %d, potential %d", position_name(p->position),
                  p->club_name.empty() ? "no club" : p->club_name.c_str(), p->overall, p->potential);
    g_state.headline = head;

    // header: name, one line of facts (the ID on hover), Undo
    ImGui::TextUnformatted(p->name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player ID %lld", static_cast<long long>(p->playerid));
    ImGui::SameLine();
    theme::muted_text("%s", head);
    {
        const size_t n = app.undo_count(p->playerid);
        char ul[48];
        std::snprintf(ul, sizeof(ul), "Undo (%zu)###pp_undo", n);
        const float w = ImGui::CalcTextSize(ul).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        const float room = ImGui::GetContentRegionAvail().x - w;
        if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
        if (n == 0) ImGui::BeginDisabled();
        if (ImGui::SmallButton(ul)) app.undo(p->playerid);
        if (n == 0) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Puts back the last change made to this player");
    }
    // 2.20: a head id written alone: the fields that go with it, through the chooser's safe give (ui_faces.h)
    head_link_banner(app, *p, "##pp_hl");
    ImGui::Separator();

    looks_card(app, *t, *p);
    skills_card(app, *t, *p);
    position_card(app, *t, *p);
    contract_card(app, *t, *p);
    name_card(app, *t, *p);
    if (ImGui::CollapsingHeader("More###pp_more")) {
        g_state.more_open = true;
        more_section(app, *t, *p);
    }
}

}  // namespace

bool player_page_matches(const PlayerRow& p, const PlayerPageFilter& f, int64_t user_team) {
    if (f.my_club && (user_team <= 0 || p.club != user_team)) return false;
    if (f.free_agents && p.club > 0 && p.club != move_rules::kFreeAgents) return false;
    if (f.position >= 0) {
        bool any = false;
        for (int k = 0; k < 7 && !any; ++k) any = p.positions[k] == f.position;
        if (!any) return false;
    }
    if (!f.text.empty()) {
        const std::string q = lower_ascii(f.text);
        if (digits_only(q) && std::to_string(p.playerid) == q) return true;
        if (lower_ascii(p.name).find(q) == std::string::npos && lower_ascii(p.club_name).find(q) == std::string::npos) return false;
    }
    return true;
}

const PlayerPageState& player_page_state() { return g_state; }


void draw_player_page(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    shell::page::frame(app, select_panel, editor_panel, nullptr, nullptr);
}

}  // namespace turbo
