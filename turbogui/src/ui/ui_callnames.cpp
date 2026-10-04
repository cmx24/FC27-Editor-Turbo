// FC 27 LE Turbo GUI - "Name and commentary": which name the commentators say for a player, in the commentary language
// the game has loaded, and a picker of the names (name ids) and players (real bank) that trigger a callname.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "imgui.h"

namespace turbo {

static const ImVec4 kGood(0.55f, 0.95f, 0.55f, 1.0f);
static const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);

static void language_combo(App& app, float width) {
    const CallnameLang* act = app.callnames.active();
    std::string cur = act ? act->code : std::string("none found");
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##cmlang", cur.c_str())) {
        for (const auto& l : app.callnames.languages()) {
            char lbl[128];
            std::snprintf(lbl, sizeof(lbl), "%s%s%s", l.code.c_str(), l.installed ? " (installed)" : "",
                          l.has_list ? "" : " [no list]");
            if (ImGui::Selectable(lbl, act && act->code == l.code)) {
                app.callnames.set_active(l.code);
                app.gui_settings["commentary"]["language"] = l.code;
                app.save_gui_settings();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commentary language packs found in the game folder (commentaryfull_<lang>.toc). The game plays one of\n"
                          "them (Settings > Audio > Commentary language): choose the same one here. Lists of spoken ids per\n"
                          "language: turbo_output\\callnames\\<lang>.csv");
}

// Write the chosen name id into the binding field (the one the commentary follows)
static void assign_nameid(App& app, const Table& t, uint64_t rec, int64_t nameid, int target /*0 auto, 1 last, 2 common*/) {
    int64_t common = app.db.get_int(t, rec, "commonnameid", 0);
    const char* field = target == 1 ? "lastnameid" : target == 2 ? "commonnameid" : (common > 0 ? "commonnameid" : "lastnameid");
    const Field* f = t.field(field);
    if (!f) {
        app.notify(std::string(field) + " is not in the players table", true);
        return;
    }
    if (app.edit(t, rec, *f, Value::of_int(nameid)))
        app.notify(std::string(field) + " = " + std::to_string(nameid) + " (" + app.model.name_text(nameid) + ")");
}

// Copy the name ids of a player whose name is recorded (real bank)
static void assign_player_names(App& app, const Table& t, uint64_t rec, int64_t donor, bool first_too) {
    const PlayerRow* d = app.model.player(donor);
    if (!d) {
        app.notify("player " + std::to_string(donor) + " is not in the database", true);
        return;
    }
    std::vector<const char*> fields = {"lastnameid", "commonnameid"};
    if (first_too) fields.push_back("firstnameid");
    int n = 0;
    for (const char* fn : fields) {
        const Field* f = t.field(fn);
        if (!f) continue;
        Value v;
        if (!app.db.get(t, d->rec, *f, v)) continue;
        if (app.edit(t, rec, *f, v)) ++n;
    }
    app.notify(std::to_string(n) + " name ids copied from " + d->name + " (" + std::to_string(donor) + ")");
}

static void picker_modal(App& app, const Table& t, uint64_t rec, int64_t playerid) {
    static char search[64] = "";
    static int show = 0;  // 0 both, 1 names, 2 players
    static int target = 0;
    static bool first_too = false;
    static std::vector<CallnameCandidate> cands;
    static std::string built_for;
    static int built_gen = -1;
    ImGui::SetNextWindowSize(ImVec2(S(760.0f), S(540.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose a spoken name", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    const CallnameLang* L = app.callnames.active();
    std::string key = (L ? L->code : "") + "|" + std::to_string(L ? L->generic.size() + L->real.size() : 0);
    if (key != built_for || built_gen != app.gen) {
        built_for = key;
        built_gen = app.gen;
        cands = app.callnames.candidates([&](int64_t nid) { return app.model.name_text(nid); },
                                         [&](int64_t pid) { return app.model.player_name(pid); });
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Language");
    ImGui::SameLine();
    language_combo(app, S(200.0f));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##cmsearch", "name (at least 2 letters)", search, sizeof(search));
    ImGui::SameLine();
    ImGui::RadioButton("Names", &show, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Players", &show, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Both", &show, 0);
    if (L && !L->has_list)
        ImGui::TextColored(kWarn, "No list for %s in turbo_output\\callnames: showing every name the database's commentarynames "
                                  "table knows (not verified for this language).", L->code.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Write a name id to");
    ImGui::SameLine();
    ImGui::RadioButton("the binding field (common name when set, else last name)", &target, 0);
    ImGui::SameLine();
    ImGui::RadioButton("last name", &target, 1);
    ImGui::SameLine();
    ImGui::RadioButton("common name", &target, 2);
    ImGui::Checkbox("Also copy the first name when a player is chosen", &first_too);

    std::string q = Callnames::lower(search);
    int64_t qid = 0;
    bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
    if (numeric) qid = std::atoll(q.c_str());
    std::vector<const CallnameCandidate*> rows;
    if (q.size() >= 2 || numeric) {
        for (const auto& c : cands) {
            if (show == 1 && c.real) continue;
            if (show == 2 && !c.real) continue;
            if (numeric ? (c.id != qid && c.commentaryid != qid) : c.lname.find(q) == std::string::npos) continue;
            rows.push_back(&c);
            if (rows.size() >= 500) break;
        }
    }
    size_t n_names = 0, n_real = 0;
    for (const auto& c : cands) (c.real ? n_real : n_names)++;
    ImGui::TextDisabled("%zu names and %zu players with a callname in %s%s", n_names, n_real, L ? L->code.c_str() : "?",
                        q.size() < 2 && !numeric ? "  -  type at least 2 letters" : "");
    if (ImGui::BeginTable("##cmrows", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableSetupColumn("Commentary id", ImGuiTableColumnFlags_WidthFixed, S(100.0f));
        ImGui::TableSetupColumn("Spoken as", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(rows.size()));
        const CallnameCandidate* picked = nullptr;
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const CallnameCandidate* c = rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char lbl[96];
                std::snprintf(lbl, sizeof(lbl), "%s##%s%lld", c->name.c_str(), c->real ? "p" : "n", static_cast<long long>(c->id));
                if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_SpanAllColumns)) picked = c;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c->real ? "player" : "name id");
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(c->id));
                ImGui::TableNextColumn();
                if (c->commentaryid) ImGui::Text("%lld", static_cast<long long>(c->commentaryid));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c->real ? "(recorded for this player)" : c->text.c_str());
            }
        }
        ImGui::EndTable();
        if (picked) {
            if (picked->real) assign_player_names(app, t, rec, picked->id, first_too);
            else assign_nameid(app, t, rec, picked->id, target);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::TextDisabled("Name ids: the commentators say the callname bound to that name. Players: their name is recorded under "
                        "their own id; copying their name ids makes the generic callname of that name play when it exists.");
    ImGui::SameLine(ImGui::GetWindowWidth() - S(90.0f));
    if (ImGui::Button("Close##cm")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void callname_section(App& app, const Table& t, uint64_t rec, int64_t playerid) {
    // name ids with their text
    for (const char* fn : {"firstnameid", "lastnameid", "commonnameid", "playerjerseynameid"}) {
        const Field* f = t.field(fn);
        if (!f) continue;
        std::string lbl = field_label(fn);
        field_editor(app, t, rec, *f, lbl.c_str(), S(90.0f));
        int64_t id = app.db.get_int(t, rec, fn, 0);
        ImGui::SameLine();
        if (id > 0) ImGui::TextDisabled("%s", app.model.name_text(id).c_str());
        else ImGui::TextDisabled("(none)");
    }
    int64_t common = app.db.get_int(t, rec, "commonnameid", 0);
    int64_t last = app.db.get_int(t, rec, "lastnameid", 0);
    CallnameInfo ci = app.callnames.info(playerid, common, last);
    const CallnameLang* L = app.callnames.active();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Commentary language");
    ImGui::SameLine(S(150.0f));
    language_combo(app, S(170.0f));
    ImGui::SameLine();
    if (ImGui::Button("Choose a spoken name...")) ImGui::OpenPopup("Choose a spoken name");
    picker_modal(app, t, rec, playerid);
    if (!app.callnames.indexed()) {
        ImGui::TextDisabled("playernames.commentaryid is not readable in this database.");
        return;
    }
    if (ci.real_spoken) {
        ImGui::TextColored(kGood, "Spoken: the commentators have this player's own name recorded (real bank, %s).", L ? L->code.c_str() : "?");
    }
    if (ci.generic_spoken) {
        ImGui::TextColored(ci.unverified ? kWarn : kGood, "%s: \"%s\" (commentary id %lld via %s %lld%s)%s",
                           ci.unverified ? "Known to the database" : "Spoken", ci.text.empty() ? "?" : ci.text.c_str(),
                           static_cast<long long>(ci.commentaryid), ci.mapped ? "playernamemap for player" : ci.binding_field,
                           static_cast<long long>(ci.mapped ? playerid : ci.binding_nameid),
                           ci.mapped ? "" : "", ci.unverified ? " - no list for this language, audio not verified" : "");
    } else if (!ci.real_spoken) {
        if (ci.commentaryid > kNoCommentary)
            ImGui::TextColored(kWarn, "Not spoken in %s: \"%s\" (commentary id %lld of %s %lld) has no audio in this language.",
                               L ? L->code.c_str() : "?", ci.text.empty() ? "?" : ci.text.c_str(), static_cast<long long>(ci.commentaryid),
                               ci.binding_field, static_cast<long long>(ci.binding_nameid));
        else
            ImGui::TextColored(kWarn, "Not spoken: %s %lld (%s) has no callname. Choose a spoken name to bind one.", ci.binding_field,
                               static_cast<long long>(ci.binding_nameid), app.model.name_text(ci.binding_nameid).c_str());
    }
    if (app.db.table("editedplayernames") && app.model.player(playerid) && app.model.player(playerid)->name != app.model.name_text(ci.binding_nameid) &&
        common <= 0)
        ImGui::TextDisabled("The shown name comes from editedplayernames; the spoken one follows the name ids above.");
}

void callnames_status(App& app) {
    ImGui::SeparatorText("Commentary (spoken names)");
    ImGui::Text("Game folder: %s", app.game_dir.empty() ? "unknown (set commentary.game_dir in gui_settings.json)" : app.game_dir.string().c_str());
    const auto& langs = app.callnames.languages();
    if (langs.empty()) ImGui::TextDisabled("No commentary language pack found (commentaryfull_<lang>.toc) and no list in %s",
                                           app.callnames.lists_dir().string().c_str());
    for (const auto& l : langs) {
        ImGui::Text("  %s: %s%s", l.code.c_str(), l.installed ? "installed" : "not installed",
                    l.has_list ? (", list: " + std::to_string(l.generic.size()) + " commentary ids, " + std::to_string(l.real.size()) + " players").c_str()
                               : ", no list (turbo_output\\callnames\\<lang>.csv with a commentaryid or playerid column)");
    }
    const CallnameLang* act = app.callnames.active();
    ImGui::Text("Active: %s | commentary texts from Lua: %zu", act ? act->code.c_str() : "none", app.callnames.commentary_texts());
}

}  // namespace turbo
