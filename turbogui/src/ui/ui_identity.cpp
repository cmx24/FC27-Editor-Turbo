// FC 27 LE Turbo GUI - team identity panels: name (live, core/teamname_override.h), colours, crest (drawn inside the
// Teams tab).
#include "ui_identity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include "app.h"
#include "core/teamnames.h"
#include "imgui.h"
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

TeamNameSave save_team_name(App& app, int64_t teamid, const std::string& name, const std::string& short_name, const std::string& code) {
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

    // 1. Turbo's names: published to the hook first (the game's next lookup shows them), then saved for the next starts
    std::string problems, err;
    if (!app.team_names_keep(e, &err)) problems += "; " + err;
    res.live = app.team_names_live();
    // 2. the database name (what Turbo's lists and other tools read)
    if (nf) {
        if (app.edit(*t, tr->rec, *nf, v)) res.detail = "teams.teamname written";
        else problems += "; teams.teamname not written";
    } else {
        res.detail = "teams.teamname not in this database";
    }
    // 3. Live Editor's file: what the game shows after Live Editor's next start when Turbo's hook is off
    bool csv_ok = false;
    TeamNamesCsv csv;
    const fs::path file = team_names_file(app.bridge.root());
    if (!csv.load(file, &err)) {
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
    res.ok = res.live || csv_ok;
    res.warning = !problems.empty();
    if (res.live) {
        res.line = "Saved: shown in the game now.";
        if (!problems.empty()) res.line += " Not written: " + problems.substr(2) + ".";
    } else if (csv_ok) {
        res.line = "Saved. The game shows it after Live Editor's next start (live names are off: " + app.team_names_why_off() + ").";
        if (!problems.empty()) res.line += " Not written: " + problems.substr(2) + ".";
    } else {
        res.line = "Not saved: " + problems.substr(2) + " (live names are off: " + app.team_names_why_off() + ").";
    }
    res.detail = full + " | " + e.text[tnames::Abbr15] + " | " + e.text[tnames::Abbr10] + " | " + e.text[tnames::Abbr3] + ": " +
                 (res.live ? "given to the game now" : "not live") + "; " + res.detail + problems;
    app.log("team name " + std::to_string(teamid) + ": " + res.detail);
    return res;
}

// The Name tab: one form, one Save
struct NameForm {
    int64_t teamid = 0;
    char name[64] = "";
    char short_name[64] = "";  // up to 15 letters
    char code[16] = "";        // up to 3 letters
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
    std::string name, short_name, code;
    if (const tnames::Entry* e = app.team_names.find(teamid)) {
        name = e->text[tnames::Full];
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
    std::snprintf(g_name.name, sizeof(g_name.name), "%s", name.c_str());
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
    ImGui::Spacing();
    const float label_w = S(120.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(320.0f));
    ImGui::InputText("##nfull", g_name.name, sizeof(g_name.name));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The club's full name (at most 59 bytes)");
    const std::string hint15 = team_short_form(g_name.name, tnames::kMaxLen[tnames::Abbr15]);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Short name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##nshort", hint15.c_str(), g_name.short_name, sizeof(g_name.short_name));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists and fixtures (at most 15 letters); empty = made from the name");
    const std::string hint3 = team_code_from(g_name.name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("3-letter code");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(S(80.0f));
    ImGui::InputTextWithHint("##ncode", hint3.c_str(), g_name.code, sizeof(g_name.code));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The scoreboard in matches; empty = made from the name");
    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        TeamNameSave r = save_team_name(app, teamid, g_name.name, g_name.short_name, g_name.code);
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
    switch (type) {
        case 0: return "Home";
        case 1: return "Away";
        case 2: return "Third";
        case 3: return "Goalkeeper home";
        case 4: return "Goalkeeper away";
        case 5: return "Goalkeeper third";
        case 6: return "Fourth";
        default: return "Kit";
    }
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

void team_colours_editor(App& app, const Table& t, uint64_t rec, int64_t teamid) {
    ImGui::TextDisabled("Preview");
    ImGui::SameLine(S(170.0f));
    swatch_row(app, t, rec, {"teamcolor1", "teamcolor2", "teamcolor3"}, S(34.0f));
    colour_editor(app, t, rec, "teamcolor1", "Team colour 1");
    colour_editor(app, t, rec, "teamcolor2", "Team colour 2");
    colour_editor(app, t, rec, "teamcolor3", "Team colour 3");
    ImGui::SeparatorText("Stadium");
    colour_editor(app, t, rec, "goalnetstanchioncolor1", "Goal net stanchion 1");
    colour_editor(app, t, rec, "goalnetstanchioncolor2", "Goal net stanchion 2");
    field_grid(app, t, rec, {"stadiumgoalnetstyle", "stadiumgoalnetpattern", "pitchcolor", "pitchlinecolor", "cornerflagpolecolor", "jerseytype"},
               "##stadcol", 3);

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
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "%s kit (type %lld, kit id %lld)", kit_type_name(k.second), static_cast<long long>(k.second),
                      static_cast<long long>(app.db.get_int(*kt, k.first, "teamkitid", 0)));
        ImGui::SetNextItemOpen(k.second == 0, ImGuiCond_Once);
        if (ImGui::CollapsingHeader(hdr)) {
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
            field_grid(app, *kt, k.first, {"teamcolorprimpercent", "teamcolorsecpercent", "teamcolortertpercent", "jerseynameoutlinewidth",
                                           "jerseynamefonttype", "numberfonttype", "jerseytemplateindex", "shortstemplateindex", "sockstemplateindex"},
                       "##kitgrid", 3);
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Written to the career database at once (range-checked); the game uses them when a kit or screen is loaded again.");
    ImGui::TextDisabled("FC 27 reloads kit colours at every career load: Turbo keeps the kit colours set here and writes them again then.");
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
    ImGui::BeginChild("##crestgrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders);
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

void crest_editor(App& app, int64_t teamid) {
    CrestEditor& ed = g_crest;
    if (ed.teamid != teamid) {
        std::string keep = ed.browse_dir.string();
        ed = CrestEditor();
        ed.teamid = teamid;
        ed.browse_dir = keep;
        ed.next_plan = 0.0;
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
        if (ImGui::BeginTable("##crestfiles", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
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
            ImGui::BeginChild("##crestfiles2", ImVec2(0, S(120.0f)), ImGuiChildFlags_Borders);
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
    team_names_service->publish(team_names);
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
