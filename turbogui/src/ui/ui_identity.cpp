// FC 27 LE Turbo GUI - team identity panels: name (live, core/teamname_override.h), colours, crest (drawn inside the
// Teams tab).
#include "ui_identity.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include "app.h"
#include "core/field_labels.h"
#include "core/teamnames.h"
#include "imgui.h"
#include "ui_asset_browser.h"
#include "ui_club_fields.h"
#include "ui_images.h"

namespace turbo {

namespace fs = std::filesystem;

static const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);
static const ImVec4 kGood(0.55f, 0.95f, 0.55f, 1.0f);

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static void placeholder_box(float side, const char* text) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(p, ImVec2(p.x + side, p.y + side), ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, side - 4.0f);
    ImGui::Dummy(ImVec2(side, side));
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + (side - ts.x) * 0.5f, p.y + (side - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                text, nullptr, side - 4.0f);
}

static bool read_bytes(const fs::path& f, std::vector<uint8_t>& out) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return !out.empty();
}

// ================================================================ name
// Short forms made from a longer name: the whole name when it fits, else cut at the last word end (a space or a dash)
// that keeps at least half of the letters, else cut (never inside a UTF-8 sequence)
std::string team_short_form(const std::string& name, size_t max_chars) {
    const std::string s = clean_team_name(name, 60);
    const std::string cut = clean_team_abbr(s, max_chars);
    if (cut.size() == s.size() || s[cut.size()] == ' ' || s[cut.size()] == '-') return cut;  // fits, or the cut ends a word
    const size_t sp = cut.find_last_of(" -");
    if (sp != std::string::npos && sp >= cut.size() / 2) return clean_team_abbr(cut.substr(0, sp), max_chars);
    return cut;
}

// The scoreboard code: the first three letters or digits of the name, upper case ("Everton Blues" -> "EVE")
std::string team_code_from(const std::string& name) {
    std::string letters;
    const std::string s = clean_team_name(name, 60);
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80 && !std::isalnum(u)) continue;  // spaces, dots, dashes
        letters += c;
    }
    return tnames::utf8_upper_basic(clean_team_abbr(letters, 3));
}

// Letters (UTF-8 characters) in a value once cleaned the way a Save cleans it (no ';', trimmed), before any cut to a cap. A cut is
// reported only when this is over the cap: upper-casing, a trailing space or a ';' never counts as letters dropped.
static size_t abbr_letters(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : clean_team_abbr(s, 1024))
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

TeamNameSave save_team_name(App& app, int64_t teamid, const std::string& name, const std::string& short_name, const std::string& code,
                            const std::string& long_name) {
    TeamNameSave res;
    const Table* t = app.db.table("teams");
    const TeamRow* tr = app.model.team(teamid);
    if (!t || !tr) {
        res.line = "Not saved: club " + std::to_string(teamid) + " is not in the loaded database.";
        return res;
    }
    const Field* nf = t->field("teamname");
    // the field's own limit (59 bytes in FC 27) is what the database, Live Editor's file and the game all get
    const size_t max_bytes = nf && nf->max_len() > 1 ? std::min<size_t>(nf->max_len() - 1, tnames::kMaxLen[tnames::Full]) : tnames::kMaxLen[tnames::Full];
    const std::string full = clean_team_name(name, max_bytes);
    if (full.empty()) {
        res.line = "Not saved: the name is empty.";
        return res;
    }
    const Value v = Value::of_str(full);
    if (nf) {
        const std::string verr = Database::validate(*nf, v);
        if (!verr.empty()) {
            res.line = "Not saved: " + verr;
            return res;
        }
    }
    tnames::Entry e;
    e.teamid = teamid;
    e.text[tnames::Full] = full;
    const std::string s15 = clean_team_abbr(short_name, tnames::kMaxLen[tnames::Abbr15]);
    e.text[tnames::Abbr15] = s15.empty() ? team_short_form(full, tnames::kMaxLen[tnames::Abbr15]) : s15;
    e.text[tnames::Abbr10] = team_short_form(e.text[tnames::Abbr15], tnames::kMaxLen[tnames::Abbr10]);
    const std::string c3 = tnames::utf8_upper_basic(clean_team_abbr(code, tnames::kMaxLen[tnames::Abbr3]));
    e.text[tnames::Abbr3] = c3.empty() ? team_code_from(full) : c3;
    const std::string lng = tnames::clean_long_name(long_name);
    if (lng != full) e.long_name = lng;  // the same as the name = not set (it follows the name)
    // 2.15: a name saved in an identified career belongs to it (core/reapply.h scopes); else not assigned (every career)
    e.scope = app.reapply.edit_scope();
    const bool career_only = !e.scope.empty();

    // 1. Turbo's names: published to the hook first (the game's next lookup shows them), then saved for the next starts
    std::string problems, err;
    const bool kept = app.team_names_keep(e, &err);
    if (!kept) problems += "; " + err;
    res.live = app.team_names_live();
    // 2. the database name (what Turbo's lists and other tools read)
    if (nf) {
        if (app.edit(*t, tr->rec, *nf, v)) res.detail = "teams.teamname written";
        else problems += "; teams.teamname not written";
    } else {
        res.detail = "teams.teamname not in this database";
    }
    // 3. Live Editor's file: what the game shows after Live Editor's next start when Turbo's hook is off. 2.15: never for a
    // name of one career (Live Editor reads its file in every career)
    bool csv_ok = false;
    TeamNamesCsv csv;
    const fs::path file = team_names_file(app.bridge.root());
    if (career_only) {
        res.detail += "; custom_team_names.csv not written (a name of this career only: Live Editor's file is read in every career)";
    } else if (!csv.load(file, &err)) {
        problems += "; custom_team_names.csv: " + err;
    } else {
        csv.set_team_names(teamid, e.text[tnames::Full], e.text[tnames::Abbr3], e.text[tnames::Abbr10], e.text[tnames::Abbr15]);
        fs::path backup;
        if (csv.save(file, team_names_backup_dir(app.bridge.root()), &err, &backup)) {
            csv_ok = true;
            res.detail += "; custom_team_names.csv updated";
            if (!backup.empty()) res.detail += " (previous file copied to " + backup.filename().string() + ")";
        } else {
            problems += "; custom_team_names.csv: " + err;
        }
    }
    res.ok = res.live || csv_ok || (career_only && kept);
    res.warning = !problems.empty();
    if (res.live) {
        res.line = career_only ? "Saved for this career only (" + app.career_label + "): shown in the game now." : "Saved: shown in the game now.";
        if (!problems.empty()) res.line += " Not written: " + problems.substr(2) + ".";
    } else if (career_only && kept) {
        res.line = "Saved for this career only. The game shows it while Turbo's live names are on (now off: " + app.team_names_why_off() + ").";
        if (!problems.empty()) res.line += " Not written: " + problems.substr(2) + ".";
    } else if (csv_ok) {
        res.line = "Saved. The game shows it after Live Editor's next start (live names are off: " + app.team_names_why_off() + ").";
        if (!problems.empty()) res.line += " Not written: " + problems.substr(2) + ".";
    } else {
        res.line = "Not saved: " + problems.substr(2) + " (live names are off: " + app.team_names_why_off() + ").";
    }
    // E14: say it when typed letters were dropped (the Abbreviation box takes 4 letters, the Short name 15)
    if (res.ok) {
        std::string cuts;
        if (abbr_letters(code) > tnames::kMaxLen[tnames::Abbr3])
            cuts += " Abbreviation cut to " + std::to_string(tnames::kMaxLen[tnames::Abbr3]) + " letters.";
        if (abbr_letters(short_name) > tnames::kMaxLen[tnames::Abbr15])
            cuts += " Short name cut to " + std::to_string(tnames::kMaxLen[tnames::Abbr15]) + " letters.";
        if (!cuts.empty()) {
            res.line += cuts;
            res.warning = true;
        }
    }
    res.detail = full + (e.long_name.empty() ? "" : " (long: " + e.long_name + ")") + " | " + e.text[tnames::Abbr15] + " | " +
                 e.text[tnames::Abbr10] + " | " + e.text[tnames::Abbr3] + ": " +
                 (res.live ? "given to the game now" : "not live") + "; " + res.detail + problems;
    app.log("team name " + std::to_string(teamid) + ": " + res.detail);
    return res;
}

// The Name tab: one form, one Save
struct NameForm {
    int64_t teamid = 0;
    char name[64] = "";        // the display name (TeamName_<id>, teams.teamname)
    char long_name[128] = "";  // Turbo's only (FC 27 has no string for it)
    char short_name[64] = "";  // up to 15 letters
    // up to kMaxLen[Abbr3] (4) letters; room for 4 bytes a letter, so a longer typed code reaches Save and is reported as cut
    char code[tnames::kMaxLen[tnames::Abbr3] * 4 + 1] = "";
    TeamNameSave last;         // the last Save (shown under the button)
    bool saved = false;
};
static NameForm g_name;
static TeamNameTabState g_name_state;

const TeamNameTabState& team_name_tab_state() { return g_name_state; }

// What the game shows for the club now, as far as Turbo knows: its own names, else Live Editor's file, else the database
static void load_name_form(App& app, const Table& t, uint64_t rec, int64_t teamid) {
    g_name = NameForm();
    g_name.teamid = teamid;
    std::string name, long_name, short_name, code;
    if (const tnames::Entry* e = app.team_names.find_in_effect(teamid, app.reapply.career)) {
        name = e->text[tnames::Full];
        long_name = e->long_name;
        short_name = e->text[tnames::Abbr15];
        code = e->text[tnames::Abbr3];
    } else {
        TeamNamesCsv csv;
        std::string a10;
        if (csv.load(team_names_file(app.bridge.root()))) csv.team_names(teamid, name, code, a10, short_name);
    }
    if (name.empty()) {
        Value v;
        if (const Field* f = t.field("teamname"))
            if (app.db.get(t, rec, *f, v)) name = v.s;
    }
    // A short form or code Turbo made from the name is left empty, so it follows the name on the next Save (only what the
    // user typed is kept); old3 is the 3-letter rule of 1.0.x / 1.1.0 (first three non-space bytes, ASCII upper case)
    if (!name.empty()) {
        std::string old3;
        for (char c : clean_team_name(name, 60)) {
            if (c == ' ') continue;
            old3 += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (old3.size() >= 3) break;
        }
        if (short_name == team_short_form(name, tnames::kMaxLen[tnames::Abbr15]) || short_name == clean_team_name(name, 15)) short_name.clear();
        if (code == team_code_from(name) || code == old3) code.clear();
        if (long_name == clean_team_name(name, 60)) long_name.clear();
    }
    std::snprintf(g_name.name, sizeof(g_name.name), "%s", name.c_str());
    std::snprintf(g_name.long_name, sizeof(g_name.long_name), "%s", long_name.c_str());
    std::snprintf(g_name.short_name, sizeof(g_name.short_name), "%s", short_name.c_str());
    std::snprintf(g_name.code, sizeof(g_name.code), "%s", code.c_str());
}

void team_name_editor(App& app, const Table& t, uint64_t rec, int64_t teamid) {
    if (g_name.teamid != teamid) load_name_form(app, t, rec, teamid);
    TeamNameTabState& st = g_name_state;
    st = TeamNameTabState();
    st.teamid = teamid;
    st.live = app.team_names_live();
    if (st.live) {
        st.mode_line = "Live names are on: Save shows the new name in the game at once.";
        ImGui::TextColored(kGood, "%s", st.mode_line.c_str());
    } else {
        st.mode_line = "Live names are off (" + app.team_names_why_off() + "): a saved name shows after Live Editor's next start.";
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s", st.mode_line.c_str());
        ImGui::PopStyleColor();
    }
    // 2.15: where the club's name applies (core/reapply.h scopes), with Global / This career
    {
        const tnames::Entry* cur = app.team_names.find_in_effect(teamid, app.reapply.career);
        const std::string scope = cur ? cur->scope : std::string();
        const std::string where = cur ? app.reapply.scope_label(scope) : "a name saved now: " + app.reapply.scope_label(app.reapply.edit_scope());
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Applies: %s", where.c_str());
        if (cur) {
            std::string to;
            ImGui::SameLine();
            ImGui::BeginDisabled(scope == kScopeGlobal);
            if (ImGui::SmallButton("Global##nscope")) to = kScopeGlobal;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(app.reapply.career.empty() || scope == app.reapply.edit_scope());
            if (ImGui::SmallButton("This career##nscope")) to = app.reapply.edit_scope();
            ImGui::EndDisabled();
            if (!to.empty() && app.move_team_name(teamid, scope, to)) app.notify("Team name: now " + app.reapply.scope_label(to));
        }
    }
    ImGui::Spacing();
    const float label_w = S(120.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Display name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(320.0f));
    ImGui::InputTextWithHint("##nfull", "AC Milan", g_name.name, sizeof(g_name.name));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the game's screens show (at most 59 bytes); also teams.teamname");
    const std::string shown = clean_team_name(g_name.name, 60);
    const std::string hint_long = shown.empty() ? std::string("Associazione Calcio Milan") : shown;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Long name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(320.0f));
    ImGui::InputTextWithHint("##nlong", hint_long.c_str(), g_name.long_name, sizeof(g_name.long_name));
    st.long_line = "Kept by Turbo only: FC 27 has no long-name string, so the game does not show it.";
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The official name (at most 100 bytes); empty = the same as the display name.\n%s", st.long_line.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(not shown by FC 27)");
    const std::string hint15 = team_short_form(g_name.name, tnames::kMaxLen[tnames::Abbr15]);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Short name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##nshort", hint15.c_str(), g_name.short_name, sizeof(g_name.short_name));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists and fixtures (at most 15 letters); empty = made from the name");
    const std::string hint3 = team_code_from(g_name.name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Abbreviation");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(110.0f));
    ImGui::InputTextWithHint("##ncode", hint3.c_str(), g_name.code, sizeof(g_name.code));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up to 4 letters (match scoreboard: 4 is not yet confirmed in game); longer is cut; empty = the first 3 letters of the name");
    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        TeamNameSave r = save_team_name(app, teamid, g_name.name, g_name.short_name, g_name.code, g_name.long_name);
        app.notify("Team name: " + r.line, !r.ok || r.warning);
        if (r.ok) load_name_form(app, t, rec, teamid);  // shows the short forms that were made
        g_name.last = r;
        g_name.saved = true;
    }
    if (g_name.saved) {
        st.result_line = g_name.last.line;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, g_name.last.ok && !g_name.last.warning ? kGood : kWarn);
        ImGui::TextWrapped("%s", st.result_line.c_str());
        ImGui::PopStyleColor();
        // A game screen that is already open built its text before the save: it shows the new name once it is built again
        if (g_name.last.live) {
            st.screen_line = "A game screen that is already open shows it once you leave that screen and come back.";
            ImGui::TextDisabled("%s", st.screen_line.c_str());
        }
    }
}

// ================================================================ colours
bool read_colour(App& app, const Table& t, uint64_t rec, const std::string& prefix, uint8_t rgb[3]) {
    static const char ch[3] = {'r', 'g', 'b'};
    for (int c = 0; c < 3; ++c) {
        const Field* f = t.field(prefix + ch[c]);
        Value v;
        if (!f || !app.db.get(t, rec, *f, v) || v.type != FieldType::Int) return false;
        rgb[c] = static_cast<uint8_t>(std::clamp<int64_t>(v.i, 0, 255));
    }
    return true;
}

bool write_colour(App& app, const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3], std::string* msg) {
    static const char ch[3] = {'r', 'g', 'b'};
    const Field* fields[3];
    for (int c = 0; c < 3; ++c) {
        fields[c] = t.field(prefix + ch[c]);
        if (!fields[c]) {
            if (msg) *msg = prefix + ch[c] + " is not in FC 27's " + t.name + " table";
            return false;
        }
        std::string verr = Database::validate(*fields[c], Value::of_int(rgb[c]));
        if (!verr.empty()) {
            if (msg) *msg = verr;
            return false;
        }
    }
    for (int c = 0; c < 3; ++c) {
        if (!app.edit(t, rec, *fields[c], Value::of_int(rgb[c]))) {
            if (msg) *msg = "writing " + fields[c]->name + " failed";
            return false;
        }
    }
    // FC 27 reloads teamkits at every career load: a kit colour is kept and written again then (ui_reapply.cpp); the
    // teams colours are saved with the career and are not kept
    app.remember_kit_colour(t, rec, prefix, rgb);
    if (msg) *msg = prefix + " = " + std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]);
    return true;
}

const char* kit_type_name(int64_t type) {
    // 2.3: the numbers FC 27's own kit assets use for teamkits.teamkittypetechid (2.2 and earlier said 2 = third, 3 =
    // goalkeeper home, ...). Evidence (turbo_output\KITS_IMPORT_RESEARCH.txt 1a): the 3D kit folders of the game's TOCs
    // are <kind>_<type>_0 with home_0_0 (1,419 clubs), away_1_0 (1,073), goalie_home_2_0 (1,303), third_3_0 (110),
    // fourth_4_0 (19), referee_5_0 (72), training_home_6_0, training_away_7_0, training_home_bibs_8_0,
    // training_away_bibs_9_0; the minikits j<type>_<teamid>_0.dds use the same number (FIFER's FC 26 goalkeeper kit
    // updates are all j2_*, and a club with home, away and goalkeeper kits only has j0, j1, j2)
    // 2.20: one table for the page's kit headers, the combos and the descriptor test (core/field_labels.h teamkittypetechid)
    if (const labels::NamedCodes* n = labels::named_codes("teamkittypetechid"))
        if (const char* s = labels::find_name(*n, type)) return s;
    if (const labels::NamedCodes* d = labels::described_codes("teamkittypetechid"))
        if (const char* s = labels::find_name(*d, type)) return s;
    return "Kit";
}

// Colour picker for <prefix>r/g/b. While the user drags or the picker popup is open the value is kept here and written
// once the mouse is up (or after a short pause for typed values), so game memory is not written every frame.
struct PendingColour {
    ImGuiID id = 0;
    float col[3] = {0, 0, 0};
    double since = 0.0;
};
static PendingColour g_pending;

static bool colour_editor(App& app, const Table& t, uint64_t rec, const char* prefix, const char* label) {
    uint8_t rgb[3];
    if (!read_colour(app, t, rec, prefix, rgb)) return false;
    bool wrote = false;
    ImGui::PushID(prefix);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(S(170.0f));
    ImGuiID id = ImGui::GetID("##c");
    float col[3] = {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f};
    float* p = g_pending.id == id ? g_pending.col : col;
    ImGui::SetNextItemWidth(S(230.0f));
    if (ImGui::ColorEdit3("##c", p, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar)) {
        if (g_pending.id != id) {
            g_pending.id = id;
            std::memcpy(g_pending.col, p, sizeof(g_pending.col));
        }
        g_pending.since = app.now;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%sr / %sg / %sb  (0..255 each)", prefix, prefix, prefix);
    if (g_pending.id == id) {
        bool settled = (!ImGui::IsAnyMouseDown() && app.now - g_pending.since >= 0.15) || app.now - g_pending.since >= 0.8;
        if (settled) {
            uint8_t nv[3];
            for (int c = 0; c < 3; ++c) nv[c] = static_cast<uint8_t>(std::clamp(int(std::lround(g_pending.col[c] * 255.0f)), 0, 255));
            g_pending.id = 0;
            if (nv[0] != rgb[0] || nv[1] != rgb[1] || nv[2] != rgb[2]) {
                std::string msg;
                wrote = write_colour(app, t, rec, prefix, nv, &msg);
                if (!wrote) app.notify(std::string(prefix) + ": " + msg, true);
            }
        }
    }
    ImGui::PopID();
    return wrote;
}

static void swatch(const char* id, const uint8_t rgb[3], float side) {
    ImGui::ColorButton(id, ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(side, side));
}

static void swatch_row(App& app, const Table& t, uint64_t rec, const std::vector<std::string>& prefixes, float side) {
    bool any = false;
    for (const auto& pf : prefixes) {
        uint8_t rgb[3];
        if (!read_colour(app, t, rec, pf, rgb)) continue;
        if (any) ImGui::SameLine(0, 2.0f);
        swatch(pf.c_str(), rgb, side);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %d, %d, %d", pf.c_str(), rgb[0], rgb[1], rgb[2]);
        any = true;
    }
    if (!any) ImGui::TextDisabled("(no colour fields)");
}

// ---- 2.11 K1: named font / layout choices of a kit (core/kit_fonts.h)
struct KitFontCache {
    bool valid = false;
    int gen = -1;
    kitfonts::Index idx;
};
static KitFontCache g_kitfonts;

const kitfonts::Index& kit_font_index(App& app) {
    const Table* kt = app.db.table("teamkits");
    if (g_kitfonts.valid && g_kitfonts.gen == app.gen) return g_kitfonts.idx;
    g_kitfonts.valid = true;
    g_kitfonts.gen = app.gen;
    std::vector<kitfonts::KitRow> rows;
    Snapshot snap;
    if (kt && snap.load(app.db.memory(), *kt)) {
        const Field* tf = kt->field("teamtechid");
        const Field* ty = kt->field("teamkittypetechid");
        const Field* vf[kitfonts::kKinds];
        for (int k = 0; k < kitfonts::kKinds; ++k) vf[k] = kt->field(kitfonts::field_of(static_cast<kitfonts::Kind>(k)));
        if (tf)
            for (uint32_t i : snap.valid) {
                kitfonts::KitRow r;
                r.team = snap.get_int(i, *tf);
                r.type = ty ? snap.get_int(i, *ty) : 0;
                for (int k = 0; k < kitfonts::kKinds; ++k) r.value[k] = vf[k] ? snap.get_int(i, *vf[k]) : -1;
                rows.push_back(r);
            }
    }
    g_kitfonts.idx.build(rows, [&app](int64_t team) { return app.model.team_name(team); });
    return g_kitfonts.idx;
}

bool write_kit_font(App& app, const Table& kt, uint64_t rec, const std::string& field, int64_t value, std::string* msg) {
    const Field* f = kt.field(field);
    const bool ours = kit_font_field(field) || field == kitfonts::field_of(kitfonts::Kind::NameLayout);
    if (kt.name != "teamkits" || !f || f->type != FieldType::Int || !ours) {
        if (msg) *msg = field + " is not a kit font field of FC 27's teamkits table";
        return false;
    }
    const std::string verr = Database::validate(*f, Value::of_int(value));
    if (!verr.empty()) {
        if (msg) *msg = verr;
        return false;
    }
    const int64_t tid = app.db.get_int(kt, rec, "teamtechid", -1);
    const int64_t type = app.db.get_int(kt, rec, "teamkittypetechid", 0);
    const int64_t kitid = app.db.get_int(kt, rec, "teamkitid", -1);
    if (!app.edit(kt, rec, *f, Value::of_int(value))) {
        if (msg) *msg = "writing " + field + " failed";
        return false;
    }
    g_kitfonts.valid = false;  // the wearers changed by this kit
    // FC 27 reloads teamkits at every career load: kept and written again then, like the colours (ui_reapply.cpp)
    app.remember_kit_field(tid, type, kitid, field, value, true);
    if (msg) *msg = field + " = " + std::to_string(value);
    return true;
}

// A combo of the ids by name ("291  Chelsea  -  worn by ...") with a filter box; picking writes the field
static void kit_font_picker(App& app, const Table& kt, uint64_t rec, kitfonts::Kind kind) {
    const Field* f = kt.field(kitfonts::field_of(kind));
    if (!f || f->type != FieldType::Int) return;
    const kitfonts::Index& idx = kit_font_index(app);
    const int64_t cur = app.db.get_int(kt, rec, f->name, -1);
    ImGui::PushID(f->name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kitfonts::title_of(kind));
    ImGui::SameLine(S(170.0f));
    const std::string preview = cur < 0 ? std::string("unreadable") : kitfonts::short_label(kind, cur);
    ImGui::SetNextItemWidth(S(300.0f));
    int64_t pick = -1;
    if (ImGui::BeginCombo("##kf", preview.c_str())) {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing()) {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##kff", "filter: id, name or club (try chelsea)", filter, sizeof(filter));
        std::vector<int64_t> ids = kitfonts::choice_ids(kind, idx);
        if (cur >= 0 && std::find(ids.begin(), ids.end(), cur) == ids.end()) ids.insert(std::lower_bound(ids.begin(), ids.end(), cur), cur);
        // a typed id nobody wears and the game does not name: still choosable within the field's range
        char* end = nullptr;
        const long long typed = std::strtoll(filter, &end, 10);
        if (end != filter && *end == '\0' && typed >= f->min && typed <= f->max() && std::find(ids.begin(), ids.end(), typed) == ids.end())
            ids.insert(std::lower_bound(ids.begin(), ids.end(), typed), typed);
        size_t shown = 0;
        for (int64_t id : ids) {
            const kitfonts::Entry* e = idx.find(kind, id);
            if (id != cur && !kitfonts::matches(kind, id, e, filter)) continue;
            ++shown;
            ImGui::PushID(static_cast<int>(id));
            const std::string lbl = kitfonts::choice_label(kind, id, e);
            if (ImGui::Selectable(lbl.c_str(), id == cur) && id != cur) pick = id;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", kitfonts::worn_by(e, 14).c_str(), f->name.c_str());
            if (id == cur && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled("Nothing matches.");
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        std::string tip = cur < 0 ? std::string("unreadable") : kitfonts::worn_by(idx.find(kind, cur), 14);
        tip += std::string("\n") + f->name + " (" + std::to_string(f->min) + ".." + std::to_string(f->max()) + ")";
        if (kind != kitfonts::Kind::NameLayout)
            tip += "\nNames are the game's own folder names (an inference: the folder number is taken as the id).\nNo font sample: check the kit in the game.";
        else
            tip += "\n0 = straight, 1 = arched, as the FIFA Kit Creator FAQ says (FC 26).";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (pick >= 0) {
        std::string msg;
        if (!write_kit_font(app, kt, rec, f->name, pick, &msg)) app.notify(f->name + ": " + msg, true);
    }
    ImGui::PopID();
}

// Outline width of the jersey name (0..max of the field): written when the slider is let go
static void kit_outline_width(App& app, const Table& kt, uint64_t rec) {
    const Field* f = kt.field("jerseynameoutlinewidth");
    if (!f || f->type != FieldType::Int) return;
    static ImGuiID pending_id = 0;
    static int pending = 0;
    ImGui::PushID(f->name.c_str());
    const ImGuiID id = ImGui::GetID("##ow");
    const int cur = static_cast<int>(app.db.get_int(kt, rec, f->name, 0));
    int v = pending_id == id ? pending : cur;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name outline width");
    ImGui::SameLine(S(170.0f));
    ImGui::SetNextItemWidth(S(300.0f));
    if (ImGui::SliderInt("##ow", &v, static_cast<int>(f->min), static_cast<int>(f->max()))) {
        pending_id = id;
        pending = v;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%lld..%lld): 0 = no outline", f->name.c_str(), static_cast<long long>(f->min), static_cast<long long>(f->max()));
    if (pending_id == id && ImGui::IsItemDeactivated()) {
        pending_id = 0;
        if (pending != cur) {
            std::string msg;
            if (!write_kit_font(app, kt, rec, f->name, pending, &msg)) app.notify(f->name + ": " + msg, true);
        }
    }
    ImGui::PopID();
}

void team_colours_editor(App& app, const Table& t, uint64_t rec, int64_t teamid) {
    ImGui::TextDisabled("Preview");
    ImGui::SameLine(S(170.0f));
    swatch_row(app, t, rec, {"teamcolor1", "teamcolor2", "teamcolor3"}, S(34.0f));
    colour_editor(app, t, rec, "teamcolor1", "Team colour 1");
    colour_editor(app, t, rec, "teamcolor2", "Team colour 2");
    colour_editor(app, t, rec, "teamcolor3", "Team colour 3");
}

// 2.13 (GUI revamp, Club & Kits > Stadium & extras): the stadium colours and goal net, split out of team_colours_editor unchanged
void team_stadium_editor(App& app, const Table& t, uint64_t rec) {
    ImGui::SeparatorText("Stadium");
    colour_editor(app, t, rec, "goalnetstanchioncolor1", "Goal net stanchion 1");
    colour_editor(app, t, rec, "goalnetstanchioncolor2", "Goal net stanchion 2");
    field_grid(app, t, rec, labels::page_stadium_fields(), "##stadcol", 3);
    draw_club_crowd_section(app, t, rec);  // 2.20 K3 (ui_club_fields.cpp)
    draw_home_ground_section(app, app.db.get_int(t, rec, "teamid", 0));
}

// 2.13 (Club & Kits > Kits, "Colours & numbers"): the kit colours, fonts and name layout, split out of team_colours_editor unchanged
void team_kit_colours_editor(App& app, int64_t teamid) {
    ImGui::SeparatorText("Kits (teamkits)");
    const Table* kt = app.db.table("teamkits");
    if (!kt || !kt->has("teamtechid")) {
        ImGui::TextDisabled("FC 27's database has no teamkits table (or no teamtechid field).");
        return;
    }
    static std::vector<std::pair<uint64_t, int64_t>> kits;  // rec, type
    static int64_t kits_team = -1;
    static int kits_gen = -1;
    if (kits_team != teamid || kits_gen != app.gen) {
        kits_team = teamid;
        kits_gen = app.gen;
        kits.clear();
        Snapshot snap;
        if (snap.load(app.db.memory(), *kt)) {
            const Field* tf = kt->field("teamtechid");
            const Field* ty = kt->field("teamkittypetechid");
            for (uint32_t idx : snap.valid) {
                if (snap.get_int(idx, *tf) != teamid) continue;
                kits.emplace_back(snap.addr(idx), ty ? snap.get_int(idx, *ty) : 0);
            }
            std::sort(kits.begin(), kits.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        }
    }
    if (kits.empty()) ImGui::TextDisabled("No kits for this team in teamkits.");
    for (const auto& k : kits) {
        if (!app.db.table_alive(*kt, k.first)) continue;
        ImGui::PushID(static_cast<int>(k.first & 0x7FFFFFFF));
        // 2.20: the kit by its name; the game's numbers (type, kit id) on hover only. Two rows of one type read "Home kit", "Home kit 2".
        int same_before = 0, same_all = 0;
        for (const auto& o : kits) {
            same_all += o.second == k.second;
            if (&o < &k) same_before += o.second == k.second;
        }
        char hdr[96];
        if (same_all > 1) std::snprintf(hdr, sizeof(hdr), "%s kit %d", kit_type_name(k.second), same_before + 1);
        else std::snprintf(hdr, sizeof(hdr), "%s kit", kit_type_name(k.second));
        ImGui::SetNextItemOpen(k.second == 0, ImGuiCond_Once);
        const bool kit_open = ImGui::CollapsingHeader(hdr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("teamkittypetechid = %lld, teamkitid = %lld", static_cast<long long>(k.second),
                              static_cast<long long>(app.db.get_int(*kt, k.first, "teamkitid", 0)));
        if (kit_open) {
            reapply_kit_line(app, *kt, k.first);  // the colours Turbo writes again at every career load, with Forget
            ImGui::TextDisabled("Preview");
            ImGui::SameLine(S(170.0f));
            swatch_row(app, *kt, k.first, {"teamcolorprim", "teamcolorsec", "teamcolortert", "jerseynamecolor", "jerseynumbercolorprim"}, S(28.0f));
            colour_editor(app, *kt, k.first, "teamcolorprim", "Primary");
            colour_editor(app, *kt, k.first, "teamcolorsec", "Secondary");
            colour_editor(app, *kt, k.first, "teamcolortert", "Tertiary");
            colour_editor(app, *kt, k.first, "jerseynamecolor", "Jersey name");
            colour_editor(app, *kt, k.first, "jerseynameoutlinecolor", "Name outline");
            colour_editor(app, *kt, k.first, "jerseynumbercolorprim", "Number primary");
            colour_editor(app, *kt, k.first, "jerseynumbercolorsec", "Number secondary");
            colour_editor(app, *kt, k.first, "jerseynumbercolorter", "Number tertiary");
            colour_editor(app, *kt, k.first, "shortsnumbercolorprim", "Shorts number 1");
            colour_editor(app, *kt, k.first, "shortsnumbercolorsec", "Shorts number 2");
            colour_editor(app, *kt, k.first, "shortsnumbercolorter", "Shorts number 3");
            // 2.11 K1: the fonts and the name layout by name (the game's folder names, and which clubs wear each id)
            kit_font_picker(app, *kt, k.first, kitfonts::Kind::NameFont);
            kit_font_picker(app, *kt, k.first, kitfonts::Kind::NumberFont);
            kit_font_picker(app, *kt, k.first, kitfonts::Kind::ShortsNumberFont);
            kit_font_picker(app, *kt, k.first, kitfonts::Kind::NameLayout);
            kit_outline_width(app, *kt, k.first);
            std::vector<std::string> kit_grid = {"teamcolorprimpercent", "teamcolorsecpercent", "teamcolortertpercent"};
            kit_grid.insert(kit_grid.end(), labels::page_kit_template_fields().begin(), labels::page_kit_template_fields().end());
            field_grid(app, *kt, k.first, kit_grid, "##kitgrid", 3);
            draw_kit_extras_section(app, *kt, k.first);  // 2.20 K3
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Written to the career database at once (range-checked); the game uses them when a kit or screen is loaded again.");
    ImGui::TextDisabled("FC 27 reloads kit colours at every career load: Turbo keeps the kit colours, fonts and name layout set here and writes them again then.");
    ImGui::TextDisabled("Fonts: no sample is drawn. The names are the game's own folder names; hover a font for the clubs that wear it. Not proven yet: whether a font change shows in a running career or only after the next game start.");
    reapply_status_line(app);
}

// ================================================================ crest
CrestPlan crest_plan(App& app, int64_t teamid) {
    CrestPlan plan;
    for (const CrestVariant& v : crest_variants(teamid)) {
        CrestPlan::Item it;
        it.variant = v;
        fs::path f;
        it.game = app.legacy.locate(v.path, &f, false);
        it.has_custom = !app.legacy.custom_file(v.path).empty();
        if (it.game == LegacyImages::State::Game) {
            std::vector<uint8_t> bytes;
            std::string err;
            if (!read_bytes(f, bytes)) {
                it.note = "cannot read the exported file";
            } else if (!parse_dds_format(bytes, it.fmt, &err)) {
                it.note = err;
            } else {
                ++plan.writable;
            }
        } else if (it.game == LegacyImages::State::Waiting) {
            ++plan.waiting;
            it.note = "loading from the game";
        } else if (it.game == LegacyImages::State::Missing) {
            ++plan.missing;
        } else {
            it.note = "invalid path";
        }
        plan.items.push_back(std::move(it));
    }
    return plan;
}

bool apply_crest(App& app, int64_t teamid, const Rgba& src0, const Framing& framing, bool remove_bg, int tolerance, std::string* msg) {
    if (src0.empty()) {
        if (msg) *msg = "no picture";
        return false;
    }
    CrestPlan plan = crest_plan(app, teamid);
    if (!plan.ready()) {
        if (msg) *msg = std::to_string(plan.waiting) + " of the game's crest files are not loaded yet (they load in the background while Turbo is open)";
        return false;
    }
    if (plan.writable == 0) {
        if (msg) *msg = "the game has no crest files for this team that Turbo can write";
        return false;
    }
    Rgba src = src0;
    if (remove_bg) remove_plain_background(src, tolerance);
    int written = 0;
    std::string notes;
    for (const auto& it : plan.items) {
        if (it.game != LegacyImages::State::Game || !it.note.empty()) {
            if (it.game == LegacyImages::State::Game) notes += "; " + it.variant.path + ": " + it.note;
            continue;
        }
        if (it.fmt.w != it.fmt.h) {
            notes += "; " + it.variant.path + ": not square";
            continue;
        }
        Rgba framed = frame_image(src, it.fmt.w, framing);
        std::string err;
        std::vector<uint8_t> dds = encode_dds(framed, it.fmt, &err);
        if (dds.empty()) {
            notes += "; " + it.variant.path + ": " + err;
            continue;
        }
        fs::path backup;
        if (!app.legacy.save_custom(it.variant.path, dds, &err, &backup)) {
            if (msg) *msg = it.variant.path + ": " + err + (written ? " (" + std::to_string(written) + " files already written)" : "");
            return false;
        }
        app.textures.forget(app.legacy.custom_file(it.variant.path));
        ++written;
    }
    if (msg) *msg = std::to_string(written) + " crest files written to mods\\legacy (" + std::to_string(plan.missing) +
                    " variants the game does not have were skipped)" + notes;
    return written > 0;
}

bool copy_crest(App& app, int64_t from_team, int64_t to_team, std::string* msg) {
    if (from_team == to_team) {
        if (msg) *msg = "same team";
        return false;
    }
    CrestPlan to = crest_plan(app, to_team);
    if (!to.ready()) {
        if (msg) *msg = std::to_string(to.waiting) + " of this team's crest files are not loaded yet";
        return false;
    }
    std::vector<CrestVariant> from_v = crest_variants(from_team);
    int copied = 0, waiting = 0, skipped = 0;
    for (size_t i = 0; i < to.items.size() && i < from_v.size(); ++i) {
        const auto& dst = to.items[i];
        if (dst.game != LegacyImages::State::Game) continue;  // the game never asks for this file
        fs::path f;
        LegacyImages::State st = app.legacy.locate(from_v[i].path, &f, true);
        if (st == LegacyImages::State::Waiting) {
            ++waiting;
            continue;
        }
        if (st != LegacyImages::State::Game && st != LegacyImages::State::Custom) {
            ++skipped;
            continue;
        }
        std::vector<uint8_t> bytes;
        DdsFormat fmt;
        if (!read_bytes(f, bytes) || !parse_dds_format(bytes, fmt) || fmt.w != dst.fmt.w || fmt.h != dst.fmt.h) {
            ++skipped;
            continue;
        }
        std::string err;
        if (!app.legacy.save_custom(dst.variant.path, bytes, &err)) {
            if (msg) *msg = dst.variant.path + ": " + err;
            return false;
        }
        app.textures.forget(app.legacy.custom_file(dst.variant.path));
        ++copied;
    }
    if (msg) {
        *msg = std::to_string(copied) + " crest files copied";
        if (waiting) *msg += "; " + std::to_string(waiting) + " of the other club's files are still loading (try again later)";
        if (skipped) *msg += "; " + std::to_string(skipped) + " skipped (the other club has no such file or another size)";
    }
    return copied > 0;
}

bool remove_crest(App& app, int64_t teamid, std::string* msg) {
    int removed = 0;
    for (const CrestVariant& v : crest_variants(teamid)) {
        fs::path old = app.legacy.custom_file(v.path);
        if (old.empty()) continue;
        std::string err;
        if (!app.legacy.remove_custom(v.path, &err)) {
            if (msg) *msg = v.path + ": " + err;
            return false;
        }
        app.textures.forget(old);
        ++removed;
    }
    if (msg) *msg = removed ? std::to_string(removed) + " custom crest files removed (copies in turbo_output\\crest_backups); the game's own are used again"
                            : "there is no custom crest";
    return removed > 0;
}

struct CrestEditor {
    int64_t teamid = 0;
    Rgba source;
    std::string source_label;
    Framing fr;
    bool remove_bg = false;
    int tolerance = 40;
    uint64_t version = 1, built = 0;
    Rgba preview;
    std::string error;
    fs::path browse_dir;
    char gallery_search[64] = "";
    double next_plan = 0.0;
    CrestPlan plan;
};
static CrestEditor g_crest;

static bool crest_source_file(CrestEditor& ed, const fs::path& f, const std::string& label) {
    Rgba img;
    std::string err;
    if (!load_image_file(f, img, &err)) {
        ed.error = f.filename().string() + ": " + err;
        return false;
    }
    ed.source = fit_image(img, 1024);
    ed.source_label = label;
    ed.fr = Framing();
    ed.error.clear();
    ++ed.version;
    return true;
}

// Grid of every club's crest; returns the picked team id or 0
static int64_t crest_gallery(App& app, CrestEditor& ed, int64_t exclude) {
    int64_t picked = 0;
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##crestsearch", "club name or ID", ed.gallery_search, sizeof(ed.gallery_search));
    std::string q = lower(ed.gallery_search);
    std::vector<const TeamRow*> rows;
    for (const auto& t : app.model.teams()) {
        if (t.teamid == exclude) continue;
        if (!q.empty() && lower(t.name).find(q) == std::string::npos && std::to_string(t.teamid).find(q) != 0) continue;
        rows.push_back(&t);
    }
    ImGui::TextDisabled("%zu clubs (pictures load from the game as they come into view)", rows.size());
    const float cell = S(80.0f);
    ImGui::BeginChild("##crestgrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / (cell + ImGui::GetStyle().ItemSpacing.x)));
    int nrows = int((rows.size() + size_t(cols) - 1) / size_t(cols));
    const float line = ImGui::GetTextLineHeight();
    ImGuiListClipper clip;
    clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                size_t i = size_t(r) * size_t(cols) + size_t(c);
                if (i >= rows.size()) break;
                const TeamRow* t = rows[i];
                if (c) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(static_cast<int>(t->teamid));
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                draw_legacy_picture(app, crest_main_path(t->teamid), cell, true);
                ImGui::SetCursorScreenPos(p0);
                char bid[48];
                std::snprintf(bid, sizeof(bid), "crest%lld", static_cast<long long>(t->teamid));
                if (ImGui::InvisibleButton(bid, ImVec2(cell, cell + line))) picked = t->teamid;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 clipr(p0.x, p0.y, p0.x + cell, p0.y + cell + line);
                dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 2.0f, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), t->name.c_str(), nullptr, 0.0f, &clipr);
                if (ImGui::IsItemHovered()) {
                    dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell + line), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 0.0f, 2.0f);
                    ImGui::SetTooltip("%s (%lld)", t->name.c_str(), static_cast<long long>(t->teamid));
                }
                ImGui::PopID();
                ImGui::EndGroup();
            }
        }
    }
    ImGui::EndChild();
    return picked;
}

// ---- crests in the game files (asset catalog): every crest the game ships, also of clubs no longer in the database
struct CrestAssetList {
    const assets::Catalog* catalog = nullptr;
    const assets::DbUse* use = nullptr;
    uint64_t model_version = ~0ull;
    std::vector<AssetBrowserItem> items;
    AssetBrowserState st;
    bool scope_set = false;
};
static CrestAssetList g_crest_assets;

// The picked team id, or 0
static int64_t crest_asset_browser(App& app, const AssetView& view, int64_t exclude) {
    CrestAssetList& l = g_crest_assets;
    if (l.catalog != view.catalog.get() || l.use != view.use.get() || l.model_version != app.model.version()) {
        l.catalog = view.catalog.get();
        l.use = view.use.get();
        l.model_version = app.model.version();
        auto db_name = [&app](int64_t id) {
            const TeamRow* t = app.model.team(id);
            return t ? t->name : std::string();
        };
        l.items = view.catalog ? asset_items_from_catalog(*view.catalog, assets::Family::Crest, view.use.get(), db_name)
                               : std::vector<AssetBrowserItem>{};
        if (!l.scope_set) l.st.scope = AssetScope::Hidden;  // first view: the DB clubs are in the other tab
        l.scope_set = true;
    }
    if (view.catalog && view.catalog->state() == assets::State::Stale)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Catalog built for another game build (%s): rebuild it with %s",
                           view.catalog->status().build_key.c_str(), assets::rebuild_command().c_str());
    int64_t pick = 0;
    const bool clicked = asset_browser(
        "##crestassets", l.st, l.items, pick, [&app](const AssetBrowserItem& it, float side) { draw_legacy_picture(app, it.path, side, true); },
        0, -ImGui::GetFrameHeightWithSpacing() * 1.5f, true);
    return clicked && pick != exclude ? pick : 0;
}

static std::string crest_asset_name(int64_t teamid) {
    for (const AssetBrowserItem& it : g_crest_assets.items)
        if (it.id == teamid) return it.name;
    return "Team #" + std::to_string(teamid);
}

void crest_editor(App& app, int64_t teamid) {
    CrestEditor& ed = g_crest;
    if (ed.teamid != teamid) {
        std::string keep = ed.browse_dir.string();
        ed = CrestEditor();
        ed.teamid = teamid;
        ed.browse_dir = keep;
        ed.next_plan = 0.0;
        app.textures.forget_pixels("crest_new");  // versions start again: the same number would show the last club's preview
    }
    if (ed.browse_dir.empty()) ed.browse_dir = app.bridge.root() / "turbo_crests";
    if (app.now >= ed.next_plan) {
        ed.plan = crest_plan(app, teamid);
        ed.next_plan = app.now + (ed.plan.ready() ? 2.0 : 0.5);
    }
    const float big = S(150.0f);

    // ---- current crest
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Now");
    LegacyImages::State st = draw_legacy_picture(app, crest_main_path(teamid), big, true);
    switch (st) {
        case LegacyImages::State::Custom: ImGui::TextColored(kGood, "Custom (mods\\legacy)"); break;
        case LegacyImages::State::Game: ImGui::TextDisabled("The game's own"); break;
        case LegacyImages::State::Waiting: ImGui::TextDisabled("Loading from the game..."); break;
        case LegacyImages::State::Missing: ImGui::TextDisabled("the game has no crest"); break;
        default: break;
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, S(24.0f));

    // ---- new crest preview
    if (ed.built != ed.version && !ed.source.empty()) {
        Rgba src = ed.source;
        if (ed.remove_bg) remove_plain_background(src, ed.tolerance);
        ed.preview = frame_image(src, kCrestBigSize, ed.fr);
        ed.built = ed.version;
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted("New");
    if (ed.source.empty()) {
        placeholder_box(big, "import a picture");
    } else {
        TextureCache::Pic pic = app.textures.pixels("crest_new", ed.built, ed.preview);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cellq = big / 8.0f;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                dl->AddRectFilled(ImVec2(p0.x + x * cellq, p0.y + y * cellq), ImVec2(p0.x + (x + 1) * cellq, p0.y + (y + 1) * cellq),
                                  ((x + y) & 1) ? IM_COL32(90, 90, 96, 255) : IM_COL32(60, 60, 66, 255));
        if (pic.tex && pic.w > 0) {
            float s = big / float(std::max(pic.w, pic.h));
            ImVec2 sz(pic.w * s, pic.h * s);
            ImVec2 a(p0.x + (big - sz.x) * 0.5f, p0.y + (big - sz.y) * 0.5f);
            dl->AddImage(pic.tex->GetTexRef(), a, ImVec2(a.x + sz.x, a.y + sz.y));
        }
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##crestdrag", ImVec2(big, big));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0.0f)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ed.fr.dx = std::clamp(ed.fr.dx + d.x / big, -1.5f, 1.5f);
            ed.fr.dy = std::clamp(ed.fr.dy + d.y / big, -1.5f, 1.5f);
            if (d.x != 0.0f || d.y != 0.0f) ++ed.version;
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
            ed.fr.zoom = std::clamp(ed.fr.zoom * std::pow(1.1f, ImGui::GetIO().MouseWheel), 0.2f, 6.0f);
            ++ed.version;
        }
        ImGui::TextDisabled("%s", ed.source_label.c_str());
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, S(24.0f));

    // ---- framing + actions
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Framing");
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Size", &ed.fr.zoom, 0.2f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) ++ed.version;
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Left-right", &ed.fr.dx, -1.5f, 1.5f, "%.2f")) ++ed.version;
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Up-down", &ed.fr.dy, -1.5f, 1.5f, "%.2f")) ++ed.version;
    if (ImGui::Checkbox("Remove plain background", &ed.remove_bg)) ++ed.version;
    if (ed.remove_bg) {
        ImGui::SetNextItemWidth(S(180.0f));
        if (ImGui::SliderInt("Tolerance", &ed.tolerance, 5, 120)) ++ed.version;
    }
    if (ImGui::SmallButton("Reset framing")) {
        ed.fr = Framing();
        ++ed.version;
    }
    ImGui::Spacing();
    bool can_save = !ed.source.empty() && ed.plan.ready() && ed.plan.writable > 0;
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("Save crest")) {
        std::string msg;
        bool ok = apply_crest(app, teamid, ed.source, ed.fr, ed.remove_bg, ed.tolerance, &msg);
        app.notify("Crest: " + msg, !ok);
        ed.next_plan = 0.0;
    }
    if (!can_save) ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && !can_save) {
        if (ed.source.empty()) ImGui::SetTooltip("Import a picture first");
        else if (!ed.plan.ready()) ImGui::SetTooltip("Waiting for the game's crest files (%d), so each is written in its own size and format", ed.plan.waiting);
        else ImGui::SetTooltip("The game has no crest files for this team that Turbo can write");
    }
    ImGui::SameLine();
    bool has_custom = false;
    for (const auto& it : ed.plan.items) has_custom = has_custom || it.has_custom;
    if (!has_custom) ImGui::BeginDisabled();
    if (ImGui::Button("Remove custom crest")) ImGui::OpenPopup("##rmcrest");
    if (!has_custom) ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("##rmcrest", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Remove every custom crest file of this club (copies go to turbo_output\\crest_backups)?");
        if (ImGui::Button("Remove")) {
            std::string msg;
            bool ok = remove_crest(app, teamid, &msg);
            app.notify("Crest: " + msg, !ok);
            ed.next_plan = 0.0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##rmcrest")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    if (!ed.error.empty()) ImGui::TextColored(kWarn, "%s", ed.error.c_str());

    // ---- the game's files
    if (ImGui::TreeNodeEx("The game's crest files for this club", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("%d can be written, %d loading, %d the game does not have. Each custom file keeps the original's size, pixel format and mipmaps.",
                            ed.plan.writable, ed.plan.waiting, ed.plan.missing);
        if (ImGui::BeginTable("##crestfiles", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("File");  // 2.22 G-resize: a header row
            ImGui::TableSetupColumn("Format");
            ImGui::TableSetupColumn("Custom");
            ImGui::TableHeadersRow();
            for (const auto& it : ed.plan.items) {
                if (it.game == LegacyImages::State::Missing) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(it.variant.path.c_str() + 18);  // after data/ui/imgAssets/
                ImGui::TableNextColumn();
                if (it.game == LegacyImages::State::Game && it.note.empty())
                    ImGui::Text("%d x %d %s%s", it.fmt.w, it.fmt.h, it.fmt.name(), it.fmt.mips > 1 ? (", " + std::to_string(it.fmt.mips) + " mips").c_str() : "");
                else
                    ImGui::TextDisabled("%s", it.note.c_str());
                ImGui::TableNextColumn();
                if (it.has_custom) ImGui::TextColored(kGood, "custom");
            }
            ImGui::EndTable();
        }
        if (ed.plan.waiting > 0) {
            ImGui::TextDisabled("Pictures still loading arrive in the background while Turbo is open (and as you play).");
        }
        ImGui::TreePop();
    }

    // ---- sources
    ImGui::SeparatorText("New crest from");
    if (ImGui::BeginTabBar("##crestsrc")) {
        if (ImGui::BeginTabItem("An image file")) {
            fs::path folder = app.bridge.root() / "turbo_crests";
            ImGui::TextWrapped("Put pictures in the Turbo crests folder (%s) or browse to them. PNG, JPG, BMP, TGA, DDS; a transparent background is kept.",
                               folder.string().c_str());
            if (ImGui::Button("Import crest image...")) ImGui::OpenPopup("##crestbrowser");
            fs::path chosen;
            if (file_browser_modal("##crestbrowser", ed.browse_dir, chosen)) {
                ed.browse_dir = chosen.parent_path();
                crest_source_file(ed, chosen, chosen.filename().string());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Turbo crests folder:");
            std::vector<fs::path> files = picture_files(folder);
            ImGui::BeginChild("##crestfiles2", ImVec2(0, S(120.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
            if (files.empty()) ImGui::TextDisabled("(empty)");
            for (const auto& f : files)
                if (ImGui::Selectable(f.filename().string().c_str())) crest_source_file(ed, f, f.filename().string());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Another club's crest")) {
            ImGui::TextDisabled("Click a club: its crest files are copied to this club (same sizes and formats), or use it as a picture to reframe.");
            static bool as_picture = false;
            ImGui::Checkbox("Use as a picture to reframe instead of copying the files", &as_picture);
            int64_t pick = crest_gallery(app, ed, teamid);
            if (pick) {
                if (as_picture) {
                    fs::path f;
                    LegacyImages::State s2 = app.legacy.locate(crest_main_path(pick), &f, true);
                    if (s2 == LegacyImages::State::Game || s2 == LegacyImages::State::Custom)
                        crest_source_file(ed, f, "crest of " + app.model.team_name(pick));
                    else
                        ed.error = "the crest of " + app.model.team_name(pick) + " is not loaded yet";
                } else {
                    std::string msg;
                    bool ok = copy_crest(app, pick, teamid, &msg);
                    app.notify("Crest of " + app.model.team_name(pick) + ": " + msg, !ok);
                    ed.next_plan = 0.0;
                }
            }
            ImGui::EndTabItem();
        }
        // catalog source (absent when the catalog is off or not loaded: the tabs are exactly 2.0's)
        const AssetView& view = asset_view();
        for (const AssetPictureSource& src : asset_crest_sources(view.loaded())) {
            if (ImGui::BeginTabItem(src.tab)) {
                ImGui::TextDisabled("%s. Click a crest: its files are copied to this club (same sizes and formats), or use it as a picture.",
                                    src.note);
                static bool as_picture_game = false;
                ImGui::Checkbox("Use as a picture to reframe instead of copying the files##game", &as_picture_game);
                const int64_t pick = crest_asset_browser(app, view, teamid);
                if (pick) {
                    const std::string from = crest_asset_name(pick);
                    if (as_picture_game) {
                        fs::path f;
                        LegacyImages::State s2 = app.legacy.locate(crest_main_path(pick), &f, true);
                        if (s2 == LegacyImages::State::Game || s2 == LegacyImages::State::Custom)
                            crest_source_file(ed, f, "crest of " + from);
                        else
                            ed.error = "the crest of " + from + " is not loaded yet";
                    } else {
                        std::string msg;
                        bool ok = copy_crest(app, pick, teamid, &msg);
                        app.notify("Crest of " + from + ": " + msg, !ok);
                        ed.next_plan = 0.0;
                    }
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::TextDisabled("Custom crests are files under <Live Editor>\\mods\\legacy\\data\\ui\\imgAssets\\crest*\\; the game shows them the next time the screen is drawn.");
}

// ================================================================ App: live team names (core/teamname_override.h)

void App::load_team_names() {
    const fs::path p = tnames::store_path(bridge.root());
    std::string err;
    team_names = tnames::Store{};
    team_names_error.clear();
    team_names_unreadable_ = !team_names.load(p, &err);
    // a note even when it loaded: a bad file set aside, bad entries dropped
    if (team_names_unreadable_) team_names_error = (err.empty() ? "cannot read " + p.string() : err) + ": no renamed clubs loaded";
    else team_names_error = err;
    if (!team_names_error.empty()) log("team names: " + team_names_error);
    if (!team_names.entries.empty()) log("team names: " + std::to_string(team_names.entries.size()) + " renamed clubs kept in " + p.string());
}

void App::team_names_publish() {
    if (!team_names_service) return;
    // 2.15: the names in effect in the loaded career (a career's names only in it); published again at every career load
    team_names_service->publish(team_names.in_effect(reapply.career));
    team_names_published_to_ = team_names_service;
}

bool App::team_names_keep(tnames::Entry e, std::string* err) {
    e.when = time_stamp();
    team_names.upsert(e);
    team_names_publish();  // the hook reads the new table on the game's next lookup
    const fs::path p = tnames::store_path(bridge.root());
    std::string why;
    if (team_names_unreadable_) {
        // never overwrite a file that could not be read: this session's names stay until Turbo closes
        why = "turbo_output\\team_names.json (it could not be read at start-up, left as it is; the name is kept for this session)";
    } else if (!team_names.save(p, &why)) {
        why = "turbo_output\\team_names.json (" + why + ")";
    } else {
        team_names_error.clear();
        return true;
    }
    team_names_error = "team names not saved: " + why;
    log(team_names_error);
    if (err) *err = why;
    return false;
}

bool App::move_team_name(int64_t teamid, const std::string& from, const std::string& to) {
    const tnames::Entry* e = team_names.find(teamid, from);
    if (!e || from == to || scope_kind(to) == ScopeKind::Unknown) return false;
    tnames::Entry moved = *e;
    team_names.forget(teamid, from);
    moved.scope = to;
    team_names.upsert(moved);  // over an entry already there for that scope
    team_names_publish();
    std::string why;
    if (team_names_unreadable_) {
        why = "turbo_output\\team_names.json could not be read at start-up (left as it is; the change lasts this session)";
    } else if (!team_names.save(tnames::store_path(bridge.root()), &why)) {
        why = "turbo_output\\team_names.json (" + why + ")";
    }
    if (!why.empty()) {
        team_names_error = "team names not saved: " + why;
        log(team_names_error);
        return false;
    }
    // a name of every career goes into Live Editor's file too (shown after its next start when live names are off); a name
    // of one career never does (a name the file already holds stays there)
    std::string note;
    if (scope_kind(to) != ScopeKind::Career) {
        TeamNamesCsv csv;
        std::string err;
        const fs::path file = team_names_file(bridge.root());
        if (csv.load(file, &err)) {
            csv.set_team_names(teamid, moved.text[tnames::Full], moved.text[tnames::Abbr3], moved.text[tnames::Abbr10], moved.text[tnames::Abbr15]);
            note = csv.save(file, team_names_backup_dir(bridge.root()), &err) ? "; custom_team_names.csv updated" : "; custom_team_names.csv: " + err;
        } else {
            note = "; custom_team_names.csv: " + err;
        }
    } else {
        note = "; custom_team_names.csv not changed (a name it already holds is still shown there when live names are off)";
    }
    log("team names: " + model.team_name(teamid) + " (" + std::to_string(teamid) + "): now " + reapply.scope_label(to) + note);
    return true;
}

std::string App::team_names_why_off() const {
    if (!team_names_service) return "not in this build of Turbo";
    if (team_names_service->available()) return "";
    const std::string why = team_names_service->why_off();
    return why.empty() ? "the hook is not installed" : why;
}

std::string App::team_names_status_line() const {
    const size_t n = team_names.entries.size();
    const std::string clubs = std::to_string(n) + (n == 1 ? " renamed club" : " renamed clubs");
    if (!team_names_live()) return "Live team names: off (" + team_names_why_off() + ") | " + clubs;
    const tnames::StatsSnapshot st = team_names_service->stats();
    return "Live team names: on | " + clubs + " | names given to the game " + std::to_string(st.given);
}

}  // namespace turbo
