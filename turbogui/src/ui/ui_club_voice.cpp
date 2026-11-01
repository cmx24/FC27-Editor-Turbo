// FC 27 LE Turbo GUI - Club & crest > Identity > Commentary name: in matches only, a club is spoken with another club's
// recorded name, or with none (core/club_voice.h; turbo_dev/PITWALL/briefs/4.0-club-callnames.md). Also the App's club
// commentary store (load, keep, forget), published with the voice swaps (App::voice_publish, ui_callnames.cpp).
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "app.h"
#include "core/club_voice.h"
#include "imgui.h"
#include "ui_callname_play.h"
#include "ui_club_page.h"
#include "ui_theme.h"

namespace turbo {

namespace {

const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);
const ImVec4 kGood(0.55f, 0.95f, 0.55f, 1.0f);

// The clubs with a recorded name in the loaded commentary language (turbo\callnames\clubs\<lang>.json), read once per language
struct Recorded {
    bool tried = false;
    std::string lang;
    std::vector<int32_t> ids;  // sorted
    std::string err;
};
Recorded g_rec;

// The picker of the club on show
struct Pick {
    int64_t teamid = 0;
    char search[64] = {};
    std::string shown_for = "\x01";  // the search text and language `shown` was made for
    std::vector<int32_t> shown;      // the recorded clubs that match the search, the club itself left out
    int64_t pick = -1;               // -1 = nothing chosen, 0 = no club name, > 0 = that club's recorded name
    bool all_careers = false;
    bool special = false;  // 5.0c E15: also list the special ids (999997, 9999990)
    size_t total = 0;      // recordings the language holds (the count line)
    std::string result;
    bool result_ok = true;
};
Pick g_pick;

const std::vector<int32_t>& recorded(App& app) {
    const std::string& lang = app.callnames.lang;
    if (!g_rec.tried || g_rec.lang != lang) {
        g_rec = Recorded{};
        g_rec.tried = true;
        g_rec.lang = lang;
        if (!lang.empty()) clubvoice::load_recorded(clubvoice::recorded_path(app.bridge.root(), lang), g_rec.ids, &g_rec.err);
    }
    return g_rec.ids;
}

std::string club_name(App& app, int64_t id) {
    if (id == 0) return "No club name";
    const TeamRow* t = app.model.team(id);
    return t && !t->name.empty() ? t->name : "team " + std::to_string(id);
}

// 5.0c E15: a recorded (donor) club by its readable name: the teams row, else the Live Editor custom_team_names.csv name, else "Club <id> (not in
// this database)", never "team <id>" (core/club_voice.h donor_label)
std::string donor_name(App& app, int64_t id) {
    const TeamRow* t = id > 0 ? app.model.team(id) : nullptr;
    return clubvoice::donor_label(id, t ? t->name : std::string(), id > 0 ? app.model.fallback_team_name(id) : std::string());
}
bool donor_named(App& app, int64_t id) {
    const TeamRow* t = app.model.team(id);
    return clubvoice::donor_is_named(t ? t->name : std::string(), app.model.fallback_team_name(id));
}

void refresh_shown(App& app, const std::vector<int32_t>& ids, int64_t teamid) {
    // the search, the language, the special-ids box, and what names the clubs (the teams table after a rebuild, the fallback map)
    const std::string key = std::string(g_pick.search) + "\x02" + g_rec.lang + "\x02" + (g_pick.special ? "s" : "-") + "\x02" +
                            std::to_string(app.model.version()) + "\x02" + std::to_string(app.model.fallback_team_names());
    if (key == g_pick.shown_for) return;
    g_pick.shown_for = key;
    g_pick.shown.clear();
    g_pick.total = ids.size();
    const std::string q = fold_search(g_pick.search);
    for (int32_t id : ids) {
        if (id == teamid) continue;  // its own name is "Use its own name"
        if (!g_pick.special && clubvoice::is_special_id(id)) continue;
        if (!q.empty() && fold_search(donor_name(app, id)).find(q) == std::string::npos && std::to_string(id).find(q) == std::string::npos) continue;
        g_pick.shown.push_back(id);
    }
}

void colored_wrapped(const ImVec4& c, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

}  // namespace

void club_voice_section(App& app) {
    const int64_t teamid = app.sel_team;
    if (teamid <= 0) {
        theme::muted_text("Choose a club first.");
        return;
    }
    if (g_pick.teamid != teamid) {
        g_pick = Pick{};
        g_pick.teamid = teamid;
    }
    const bool in_career = !app.reapply.career.empty();
    if (!in_career) g_pick.all_careers = true;  // no career loaded: a choice saved now is for every career
    if (!app.voice_available())
        colored_wrapped(kWarn, "Commentary changes are off (" + app.voice_why_off() + "): a saved choice is kept and used once they are on.");

    // what the commentator says now
    const std::string& lang = app.callnames.lang;
    const std::vector<int32_t>& ids = recorded(app);
    const bool own = std::binary_search(ids.begin(), ids.end(), static_cast<int32_t>(teamid));
    const clubvoice::Entry* cur = app.club_voice.find_in_effect(teamid, app.reapply.career);
    std::string says;
    if (cur && cur->voice_of == 0) says = "no club name (what the game says then is not verified)";
    else if (cur && cur->voice_of != teamid) says = "the name recorded for " + donor_name(app, cur->voice_of);
    else if (lang.empty()) says = "its own name, if the commentary has one (commentary language not known yet)";
    else if (own) says = "its own recorded name";
    else says = "no club name: the " + lang + " commentary has no recording for this club";
    // Turbo X 1.0.1 L1: Play the current club's own clip in the loaded language (the same button and wav as the list's rows: ui_callname_play.cpp,
    // turbo_output, cache, teamaudio, <lang>, <club id>.wav). Disabled, with the reason in its tooltip, when no clip was made for it.
    // TX-102 R2-O2: the button plays the clip IN EFFECT: the donor's when a donor voice is set (cur->voice_of), else the club's own;
    // "no club name" in effect: the button stays, disabled, saying why.
    if (cur && cur->voice_of == 0) {
        const float h = ImGui::GetTextLineHeight();
        char label[48];
        std::snprintf(label, sizeof(label), "##play_c%lld", static_cast<long long>(teamid));
        ImGui::BeginDisabled();
        ImGui::Button(label, ImVec2(h, h));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("No club name is in effect: there is no clip to play.");
    } else {
        callname_play_button(app, CallnameAudioKind::Club, cur && cur->voice_of > 0 ? cur->voice_of : teamid);
    }
    ImGui::SameLine();
    ImGui::TextWrapped("Commentary says: %s", says.c_str());
    const std::string cur_scope = cur ? cur->scope : std::string();
    if (cur) ImGui::TextDisabled("Applies: %s", app.reapply.scope_label(cur_scope).c_str());
    if (!app.club_voice_error.empty()) colored_wrapped(kWarn, app.club_voice_error);

    // the picker: the clubs that have a recorded name in this language, and "No club name"
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Say it with");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(280.0f));
    ImGui::InputTextWithHint("##cvsearch", "Search the clubs with a recorded name", g_pick.search, sizeof(g_pick.search));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only clubs the %s commentary has a recording for are listed", lang.empty() ? "loaded" : lang.c_str());
    if (lang.empty()) theme::muted_text("The commentary language is not known yet: the list fills once it is.");
    else if (ids.empty()) colored_wrapped(kWarn, "No list of recorded club names for " + lang + (g_rec.err.empty() ? std::string() : " (" + g_rec.err + ")"));
    // The two special ids some recordings hold are listed only on request
    bool has_special = false;
    for (int32_t id : ids)
        if (clubvoice::is_special_id(id)) has_special = true;
    if (has_special) {
        ImGui::Checkbox("Show special ids##cvspecial", &g_pick.special);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also list 999997 and 9999990: the recordings hold them, but they are not club ids of a database (what they say is not checked yet)");
    }
    refresh_shown(app, ids, teamid);
    // A fixed number of rows (about 13 visible). Never GetContentRegionAvail: this card is AutoResizeY, so the available height grows with the list
    // every frame. The old S(180) showed about 7.
    ImGui::BeginChild("##cvlist", ImVec2(0.0f, clubvoice::donor_list_height(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetStyle().WindowPadding.y)),
                      ImGuiChildFlags_Borders);
    {
        // the chosen row is marked "> " as well as highlighted (never colour alone)
        const bool sel0 = g_pick.pick == 0;
        if (ImGui::Selectable(sel0 ? "> No club name (not verified in game)##cv0" : "No club name (not verified in game)##cv0", sel0)) g_pick.pick = 0;
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(g_pick.shown.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const int32_t id = g_pick.shown[static_cast<size_t>(i)];
                const bool sel = g_pick.pick == id;
                const std::string label = (sel ? "> " : "") + donor_name(app, id) + (donor_named(app, id) ? " (" + std::to_string(id) + ")" : std::string()) +
                                          "##cv" + std::to_string(id);
                callname_play_button(app, CallnameAudioKind::Club, id);  // Preview: the club's FC 27 clip in the loaded language, or a disabled button with the reason
                ImGui::SameLine();
                if (ImGui::Selectable(label.c_str(), sel)) g_pick.pick = id;
            }
    }
    ImGui::EndChild();
    if (!lang.empty() && !ids.empty()) {
        theme::muted_text("%zu clubs have a recording in the %s commentary (showing %zu)", g_pick.total, lang.c_str(), g_pick.shown.size());
        // Turbo X: the play buttons need the decoded clips (turbo_output, cache, teamaudio, <lang>); count them every 5 s so the page says how many
        // can be heard instead of a disabled button that looks like "no audio".
        static std::string clip_dir;
        static size_t clip_count = 0;
        static double clip_checked = -1e9;
        const std::string dir = (app.turbo_output() / "cache" / "teamaudio" / lang).u8string();
        if (dir != clip_dir || app.now - clip_checked >= 5.0) {
            clip_dir = dir;
            clip_checked = app.now;
            clip_count = 0;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end; !ec && it != end; it.increment(ec))
                if (it->path().extension() == ".wav") ++clip_count;
        }
        theme::muted_text("Preview clips ready: %zu of %zu", clip_count, g_pick.total);
    }
    else theme::muted_text("%zu clubs listed", g_pick.shown.size());

    // where it applies, Save, Use its own name
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Save for");
    ImGui::SameLine();
    ImGui::BeginDisabled(!in_career);
    if (ImGui::RadioButton("This career##cvscope", !g_pick.all_careers)) g_pick.all_careers = false;
    ImGui::EndDisabled();
    if (!in_career && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("No career is loaded");
    ImGui::SameLine();
    if (ImGui::RadioButton("All careers##cvscope", g_pick.all_careers)) g_pick.all_careers = true;
    ImGui::BeginDisabled(g_pick.pick < 0);
    if (ImGui::Button("Save##cvsave")) {
        clubvoice::Entry e;
        e.teamid = teamid;
        e.voice_of = g_pick.pick;
        e.scope = g_pick.all_careers ? std::string(kScopeGlobal) : app.reapply.edit_scope();
        e.club = club_name(app, teamid);
        e.from = donor_name(app, g_pick.pick);
        const bool saved = app.club_voice_keep(e);
        g_pick.result_ok = saved;
        g_pick.result = saved ? "Saved (" + app.reapply.scope_label(e.scope) + "): " + e.club + " is now said as " + e.from +
                                    " from the next commentary line. Not yet checked in a match."
                              : app.club_voice_error;
        app.notify("Commentary name: " + g_pick.result, !saved);
    }
    ImGui::EndDisabled();
    if (g_pick.pick < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Choose a club or \"No club name\" first");
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button("Use its own name##cvundo")) {
        app.club_voice_forget(teamid, cur_scope);
        g_pick.result_ok = app.club_voice_error.empty();
        g_pick.result = g_pick.result_ok ? "Back to its own name (" + app.reapply.scope_label(cur_scope) + ")" : app.club_voice_error;
        app.notify("Commentary name: " + g_pick.result, !g_pick.result_ok);
    }
    ImGui::EndDisabled();
    if (!g_pick.result.empty()) colored_wrapped(g_pick.result_ok ? kGood : kWarn, g_pick.result);
    theme::muted_text("Matches only. Nothing is written to the database or the save.");
}

// ================================================================ App: the club commentary store (core/club_voice.h)

void App::load_club_voice() {
    const std::filesystem::path p = clubvoice::store_path(bridge.root());
    std::string err;
    club_voice = clubvoice::Store{};
    club_voice_unreadable_ = !club_voice.load(p, &err);
    // a note even when it loaded: a bad file set aside, bad entries dropped
    if (club_voice_unreadable_) club_voice_error = (err.empty() ? "cannot read " + p.string() : err) + ": no club commentary names loaded";
    else club_voice_error = err;
    if (!club_voice_error.empty()) log("club commentary: " + club_voice_error);
    if (!club_voice.entries.empty()) log("club commentary: " + std::to_string(club_voice.entries.size()) + " kept in " + p.string());
}

bool App::save_club_voice() {
    const std::filesystem::path p = clubvoice::store_path(bridge.root());
    std::string err;
    if (club_voice_unreadable_) {
        // never overwrite a file that could not be read: this session's changes stay until Turbo closes
        club_voice_error = "not saved: " + p.filename().string() + " could not be read at start-up (left as it is; the change lasts this session)";
        log("club commentary: " + club_voice_error);
        return false;
    }
    if (!club_voice.save(p, &err)) {
        club_voice_error = "not saved: " + err + " (the change lasts this session)";
        log("club commentary: " + club_voice_error);
        return false;
    }
    club_voice_error.clear();
    return true;
}

bool App::club_voice_keep(clubvoice::Entry e) {
    e.when = time_stamp();
    club_voice.upsert(e);
    voice_publish();  // the hook reads the new table on the next commentary line
    const bool saved = save_club_voice();
    log("club commentary: team " + std::to_string(e.teamid) + " said as " + (e.voice_of ? "team " + std::to_string(e.voice_of) : std::string("no club name")) +
        " (" + (e.scope.empty() ? std::string("not assigned") : e.scope) + ")");
    return saved;
}

bool App::club_voice_forget(int64_t teamid, const std::string& scope) {
    if (!club_voice.forget(teamid, scope)) return false;
    voice_publish();
    save_club_voice();
    log("club commentary: team " + std::to_string(teamid) + " back to its own name (" + (scope.empty() ? std::string("not assigned") : scope) + ")");
    return true;
}

}  // namespace turbo
