// Native tests (2.20 K3, brief 2.20-K3-club-fields): the club, crowd, stadium and kit fields (core/club_fields.h, ui/ui_club_fields.cpp).
// Included by test_main.cpp after test_club_page.h. club_fields_test::run() = the pure cases (value sets, names, refusals, the label manifest);
// club_fields_test::ui_cases() = the sections drawn on Club & Kits > Stadium & extras with a real App over the gui_world database (its teams,
// stadiums, teamstadiumlinks and teamkits tables carry the 2.20 K3 columns).
//
// PROVEN here: every value of every field's set has a plain name (never a bare code); no name exists for a value outside the set;
// crowdskintonecode refuses 50; the Live Editor names (weather, jersey fit, shorts, kit type) are used; the pages draw the sections, write a
// value, read it back, offer Undo, and leave the kept-edits store byte-identical (the club and stadium columns are kept by the career save
// itself; a kit extra is live only). NOT proven here: anything in the game - that is the Tester's W2 (tools/club_fields_livecheck.py).
#pragma once
#include <set>

#include "core/club_fields.h"
#include "ui/ui_club_fields.h"

namespace club_fields_test {

using namespace turbo;
namespace cf = turbo::clubfields;

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static const cf::Def& def(const char* table, const char* field) {
    const cf::Def* d = cf::find_def(table, field);
    if (!d) {
        std::printf("FAIL test_club_fields: no def %s.%s\n", table, field);
        std::abort();
    }
    return *d;
}

static void run() {
    run_case("club fields: every field has a title, help, a section and a value set from the stock file", [&] {
        std::set<std::string> seen;
        bool titles = true, helps = true, sets = true, asc = true, uniq = true;
        for (const cf::Def& d : cf::defs()) {
            titles = titles && d.title && d.title[0];
            helps = helps && d.help && d.help[0];
            uniq = uniq && seen.insert(std::string(d.table) + "." + d.field).second;
            const std::vector<int> v = cf::values_of(d);
            sets = sets && !v.empty();
            for (size_t i = 1; i < v.size(); ++i) asc = asc && v[i - 1] < v[i];
        }
        CHECK(titles && helps && uniq, "titles, help lines, unique fields");
        CHECK(sets && asc, "every field has a value set, ascending");
        CHECK(cf::defs().size() == 52, fmt("52 fields (%zu)", cf::defs().size()));
        size_t crowd = 0, home = 0, kit = 0;
        for (const cf::Def& d : cf::defs()) {
            if (!d.editable) continue;
            (d.section == cf::Section::Crowd ? crowd : d.section == cf::Section::HomeGround ? home : kit)++;
        }
        CHECK(crowd == 21 && home == 18 && kit == 12, fmt("21 crowd, 18 home ground, 12 kit fields (%zu, %zu, %zu)", crowd, home, kit));
        // the generated block names its source file and md5
        const std::string text = slurp(fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "core" / "club_fields.h");
        const size_t at = text.find("// source: ");
        CHECK(at != std::string::npos && text.find(" md5 ", at) != std::string::npos, "the generated block says its source file and md5");
        const size_t m = text.find(" md5 ", at);
        bool hex = m != std::string::npos;
        for (size_t i = 0; hex && i < 32; ++i) hex = std::isxdigit(static_cast<unsigned char>(text[m + 5 + i])) != 0;
        CHECK(hex, "the md5 is 32 hex digits");
        CHECK(text.find("kDomV51[]") != std::string::npos && text.find("kDomV52[]") == std::string::npos, "52 generated value arrays");
    });

    run_case("club fields: every value of every set has a plain name, none is a bare code; no name for a value outside the set", [&] {
        bool named = true, notcode = true, outside = true;
        std::string bad;
        for (const cf::Def& d : cf::defs()) {
            const std::vector<int> set = cf::values_of(d);
            for (int v : set) {
                const std::string t = cf::value_label(d, v).text;
                named = named && !t.empty();
                bool digits = !t.empty();
                for (char c : t) digits = digits && std::isdigit(static_cast<unsigned char>(c));
                if (digits || t == d.field) { notcode = false; bad = std::string(d.field) + "=" + t; }
                if (!cf::value_hover(d, v).empty() && cf::value_hover(d, v).find("Code " + std::to_string(v)) == std::string::npos) notcode = false;
            }
            for (int v = -3; v <= 1100; ++v) {
                if (cf::in_domain(d, v)) continue;
                if (!cf::value_label(d, v).text.empty() || (d.editable && cf::refusal(d, v).empty())) {
                    outside = false;
                    bad = std::string(d.field) + " " + std::to_string(v);
                }
            }
        }
        CHECK(named, "every value of every set has a name");
        CHECK(notcode, "no name is a bare number or the field's own name: " + bad);
        CHECK(outside, "no name (and a refusal) for a value outside the set: " + bad);
    });

    run_case("club fields: the Live Editor names, On/Off, Off for a 'has' field, Style N for the rest", [&] {
        auto L = [&](const char* tb, const char* fl, int v) { return cf::value_label(def(tb, fl), v).text; };
        // weather: 0 Clear ... 4 Overcast from Live Editor; 5 and 7 are in the stock set and unnamed
        CHECK(L("stadiums", "defaultweather", 0) == "Clear" && L("stadiums", "defaultweather", 1) == "Partly Clear" && L("stadiums", "defaultweather", 2) == "Rain" &&
                  L("stadiums", "defaultweather", 3) == "Snow" && L("stadiums", "defaultweather", 4) == "Overcast",
              "weather 0..4 are Live Editor's names");
        CHECK(L("stadiums", "defaultweather", 5) == "Style 5" && L("stadiums", "defaultweather", 7) == "Style 7" && L("stadiums", "defaultweather", 6).empty(),
              "weather 5 and 7 are Style N; 6 is not in the stock set: no name");
        CHECK(L("stadiums", "tod1weather", 3) == "Snow" && L("stadiums", "tod4weather", 0) == "Clear" && L("stadiums", "tod1weather", 2).empty(), "time slot weather: 0 1 3 in the set");
        CHECK(L("teamkits", "jerseyfit", 0) == "Normal" && L("teamkits", "jerseyfit", 1) == "Tight" && L("teamkits", "jerseyfit", 2).empty(), "jersey fit Normal / Tight; 2 is outside FC 27");
        CHECK(L("teamkits", "shortstyle", 0) == "GK Shorts" && L("teamkits", "shortstyle", 2) == "Style 2" && L("teamkits", "shortstyle", 1).empty(),
              "shorts: GK Shorts, Style 2; 1 (GK Pants) is not in the stock set");
        CHECK(L("teamkits", "teamkittypetechid", 0) == "Home" && L("teamkits", "teamkittypetechid", 1) == "Away" && L("teamkits", "teamkittypetechid", 2) == "GK" &&
                  L("teamkits", "teamkittypetechid", 3) == "Third" && L("teamkits", "teamkittypetechid", 4) == "Kit type 4 (unknown)",
              "kit type 0 Home 1 Away 2 GK 3 Third, 4 and up unknown");
        CHECK(L("teams", "hasvikingclap", 0) == "Off" && L("teams", "hasvikingclap", 1) == "On" && L("stadiums", "hasmexicanwave", 1) == "On", "a 0..1 switch: Off / On");
        CHECK(L("stadiums", "supportsunlicensedtifos", 1) == "On" && L("stadiums", "supportsunlicensedtifos", 0).empty(), "a one-valued set: only On");
        CHECK(L("teams", "hastifo", 0) == "Off" && L("teams", "hastifo", 1) == "Style 1" && L("teams", "hastifo", 2) == "Style 2", "a 'has' field: Off, then Style N");
        CHECK(L("teams", "stanchionflamethrower", 2) == "Style 2" && L("teams", "flamethrowercannon", 4) == "Style 4" && L("teams", "flamethrowercannon", 2).empty(),
              "flame fields: only the stock values");
        CHECK(L("teams", "crowdskintonecode", 20) == "Style 20" && L("teams", "crowdskintonecode", 90) == "Style 90", "crowd skin tones are Style N");
        CHECK(L("teams", "ballid", 55) == "Ball 55" && L("teams", "trainingstadium", 366) == "Training ground 366", "the ball and the training ground say what they are");
        CHECK(cf::value_hover(def("teams", "hastifo"), 1).find("no name") != std::string::npos && cf::value_hover(def("teams", "hastifo"), 0).find("name") != std::string::npos,
              "the hover says where a name comes from");
        CHECK(cf::value_hover(def("stadiums", "defaultweather"), 4).find("Live Editor") != std::string::npos, "the hover credits Live Editor");
        CHECK(cf::other_label(77) == "Other (77)", "a stored value outside the set is shown as Other (N)");
    });

    run_case("club fields: crowdskintonecode is {20,40,70,90} and rejects 50; the write is refused outside a set", [&] {
        const cf::Def& d = def("teams", "crowdskintonecode");
        CHECK(cf::values_of(d) == std::vector<int>({20, 40, 70, 90}), "the four tones of the stock file");
        for (int v : {20, 40, 70, 90}) CHECK(cf::refusal(d, v).empty(), fmt("%d accepted", v));
        const std::string r = cf::refusal(d, 50);
        CHECK(!r.empty() && r.find("50") != std::string::npos && r.find("Style 20") != std::string::npos && r.find("Style 90") != std::string::npos, "50 refused, the choices named: " + r);
        CHECK(!cf::refusal(def("teams", "hastifo"), 3).empty() && cf::refusal(def("teams", "hastifo"), 2).empty(), "hastifo: 3 refused, 2 accepted");
        CHECK(!cf::refusal(def("teamkits", "teamkittypetechid"), 0).empty(), "the kit type only names a kit: never written");
        CHECK(!cf::refusal(def("teams", "ballid"), 53).empty() && cf::refusal(def("teams", "ballid"), 55).empty(), "the ball: only the stock balls");
        CHECK(!cf::refusal(def("teamkits", "jerseyfit"), 2).empty(), "jersey fit 2 (Team Kit Fit) is refused: outside FC 27");
    });

    run_case("club fields: the label manifest - no table names, no developer words, no raw codes as a title", [&] {
        static const std::set<std::string> banned = {"teams",  "stadiums", "teamkits", "teamstadiumlinks", "cmsquads", "table", "tables", "lua",  "hook",
                                                      "json",   "txt",      "md5",      "ebx",              "dll",      "exe",   "path",   "hex",  "channel",
                                                      "database", "column", "row"};
        std::string hit;
        std::vector<std::string> all = cf::main_texts();
        for (const cf::Def& d : cf::defs()) {
            all.push_back(d.title);
            all.push_back(d.help);
            all.push_back(d.group);
        }
        for (const std::string& s : all)
            for (const std::string& t : tokens(s))
                if (banned.count(t)) hit = t + " in \"" + s + "\"";
        CHECK(hit.empty(), "no table name or developer word in a title, group, help or value name: " + hit);
        bool lower = true;
        for (const cf::Def& d : cf::defs()) {
            const std::string t = d.title;
            if (t == d.field) lower = false;
            for (char c : t)
                if (c == '_') lower = false;
        }
        CHECK(lower, "no title is a field name");
    });
}

// The page window of test_club_page.h takes the focus at every frame (SetNextWindowFocus), which closes a combo popup at once: this one
// is the same window without that call (same name, so the finds of the other page tests work here too)
static void draw_extra(App& app) {
    if (!club_page_test::g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##clubpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        club_page_test::g_page_fn(app);
    ImGui::End();
}
static void show_page(Ui& ui, shell::Page p) {
    club_page_test::g_page_fn = shell::registered_page(p);
    g_ui_extra = draw_extra;
    ui.frames(3);
}

// ---------------------------------------------------------------- UI
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    // under TURBO_TEST_ONLY the case that connects the app is skipped: connect here (a no-op in a full run)
    ui.frames(3);
    if (!app.connected()) app.refresh();
    ui.frames(3);
    run_case("club fields UI: Stadium & extras draws Crowd & presentation and Home ground, writes, reads back, undoes; nothing is kept", [&] {
        using shell::Page;
        club_field_undo_clear();
        const std::string store_before = slurp(reapply_store_path(le));
        const std::string kept0 = club_page_test::kept_rows(app.reapply);
        const size_t players0 = app.reapply.players.size(), callnames0 = app.reapply.callnames.size();
        const int64_t sel0 = app.sel_team;
        app.sel_team = 7;
        const Table* t = app.db.table("teams");
        const Table* st = app.db.table("stadiums");
        CHECK(t && t->field("hastifo") && t->field("stanchionflamethrower"), fmt("the test world's teams table has the K3 columns (teams %d, hastifo %d, flame %d, ntables %zu)", t ? 1 : 0, t && t->field("hastifo") ? 1 : 0, t && t->field("stanchionflamethrower") ? 1 : 0, app.db.table_names().size()));
        CHECK(st && st->field("hasmexicanwave"), fmt("the test world has the stadiums table (%d)", st ? 1 : 0));
        if (!t || !t->field("hastifo") || !t->field("stanchionflamethrower") || !st) return;
        const uint64_t rec = app.db.find(*t, "teamid", 7);
        CHECK(app.db.get_int(*t, rec, "hastifo") == 2 && app.db.get_int(*t, rec, "stanchionflamethrower") == 0, "Everton starts with hastifo 2, flame jets 0");
        show_page(ui, Page::StadiumExtras);
        ui.frames(3);
        CHECK(ui.find("Crowd & presentation###cfcrowd", "##clubpage_test") != nullptr, "the Crowd & presentation header is drawn");
        CHECK(ui.find("Home ground###cfhome", "##clubpage_test") != nullptr, "the Home ground header is drawn");
        const ItemRec* tifo = ui.find("##cf", "##clubpage_test", "hastifo");
        CHECK(tifo != nullptr, "the tifo picker is drawn");
        // pick Style 1 in the tifo combo
        if (tifo) {
            CHECK(ui.click(tifo), "combo opened");
            ui.frames(2);
            CHECK(ui.find("Off##v0") && ui.find("Style 1##v1") && ui.find("Style 2##v2"), "the three choices are named: Off, Style 1, Style 2");
            CHECK(ui.click("Style 1##v1"), "Style 1 picked");
            ui.frames(3);
        }
        CHECK(app.db.get_int(*t, rec, "hastifo") == 1, "hastifo is 1 in the database");
        CHECK(club_field_undo_depth() == 1 && club_field_undo_top_team() == 7, "one undo step, for Everton");
        // the direct writer: flame jets 0 -> 2, then refusals
        std::string msg;
        CHECK(write_club_field(app, *t, rec, "stanchionflamethrower", 2, &msg) && app.db.get_int(*t, rec, "stanchionflamethrower") == 2, "flame jets 2: " + msg);
        CHECK(!write_club_field(app, *t, rec, "crowdskintonecode", 50, &msg) && app.db.get_int(*t, rec, "crowdskintonecode") == 70 && msg.find("50") != std::string::npos,
              "crowd skin tones 50 refused, the row unchanged: " + msg);
        CHECK(write_club_field(app, *t, rec, "crowdskintonecode", 40, &msg) && app.db.get_int(*t, rec, "crowdskintonecode") == 40, "crowd skin tones 40 accepted: " + msg);
        CHECK(!write_club_field(app, *t, rec, "teamcolor1r", 5, &msg) && msg.find("not one of the club fields") != std::string::npos, "any other field is refused: " + msg);
        CHECK(!write_club_field(app, *t, rec, "hastifo", 3, &msg) && app.db.get_int(*t, rec, "hastifo") == 1, "hastifo 3 refused: " + msg);
        CHECK(write_club_field(app, *t, rec, "hastifo", 1, &msg) && msg.find("already") != std::string::npos && club_field_undo_depth() == 3,
              "writing the value it already has makes no undo step: " + msg);
        // Undo last change (the button of the section) puts the last step back: crowd skin tones 40 -> 70
        ui.frames(2);
        const ItemRec* undo = ui.find("Undo last change##cfundo_crowd", "##clubpage_test");
        CHECK(undo != nullptr, "the Undo button of the section is drawn");
        if (undo) {
            CHECK(ui.click(undo), "Undo clicked");
            ui.frames(3);
        }
        CHECK(app.db.get_int(*t, rec, "crowdskintonecode") == 70 && club_field_undo_depth() == 2, "undone: tones back to 70");
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*t, rec, "stanchionflamethrower") == 0, "undone: flame jets back to 0: " + msg);
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*t, rec, "hastifo") == 2 && club_field_undo_depth() == 0, "undone: tifo back to 2: " + msg);
        CHECK(!undo_club_field(app, &msg) && msg.find("Nothing") != std::string::npos, "nothing left to undo: " + msg);
        // the kept-edits store: untouched
        CHECK(club_page_test::kept_rows(app.reapply) == kept0 && app.reapply.players.size() == players0 && app.reapply.callnames.size() == callnames0 &&
                  slurp(reapply_store_path(le)) == store_before,
              "no reapply row was made and the store file is byte-identical");
        app.sel_team = sel0;
        club_page_test::hide(ui);
    });

    run_case("club fields UI: Home ground - the club's own stadium, shared grounds, a checkbox On/Off", [&] {
        using shell::Page;
        club_field_undo_clear();
        const int64_t sel0 = app.sel_team;
        const HomeGround e = home_ground_of(app, 7);
        CHECK(e.found && e.stadiumid == 10 && e.name == "Goodison Test Park" && e.shared_with == 0, "Everton: stadium 10 by name, alone");
        const HomeGround inter = home_ground_of(app, 241);
        CHECK(inter.found && inter.stadiumid == 30 && inter.shared_with == 1, fmt("Inter shares stadium 30 with one club (%d)", inter.shared_with));
        const HomeGround none = home_ground_of(app, 111592);
        CHECK(!none.found && !none.why.empty(), "a club with no home ground says so: " + none.why);
        const Table* st = app.db.table("stadiums");
        CHECK(st != nullptr && e.found, "the stadiums table and Everton's home ground are there");
        if (!st || !e.found) return;
        std::string msg;
        CHECK(write_club_field(app, *st, e.rec, "defaultweather", 4, &msg, 7) && app.db.get_int(*st, e.rec, "defaultweather") == 4, "weather Overcast: " + msg);
        CHECK(!write_club_field(app, *st, e.rec, "defaultweather", 6, &msg, 7) && app.db.get_int(*st, e.rec, "defaultweather") == 4, "weather 6 is not in the stock set: refused");
        CHECK(!write_club_field(app, *st, e.rec, "supportsunlicensedtifos", 0, &msg, 7) && app.db.get_int(*st, e.rec, "supportsunlicensedtifos") == 1,
              "the one-valued field stays On: 0 refused");
        // the page: draw Everton's page, tick the Mexican wave
        app.sel_team = 7;
        show_page(ui, Page::StadiumExtras);
        ui.frames(3);
        const ItemRec* wave = ui.find("##cf", "##clubpage_test", "hasmexicanwave");
        // the Home ground section is below the 21 crowd rows: scroll the page with the mouse wheel until the tick box is on screen
        for (int i = 0; i < 12 && !wave; ++i) {
            ImGui::GetIO().AddMousePosEvent(30.0f, 60.0f);
            ImGui::GetIO().AddMouseWheelEvent(0.0f, -6.0f);
            ui.frames(3);
            wave = ui.find("##cf", "##clubpage_test", "hasmexicanwave");
        }
        if (wave) {
            CHECK(app.db.get_int(*st, e.rec, "hasmexicanwave") == 0, "Everton's ground has no Mexican wave");
            CHECK(ui.click(wave), "wave ticked");
            ui.frames(3);
            CHECK(app.db.get_int(*st, e.rec, "hasmexicanwave") == 1 && club_field_undo_depth() == 2, "the stadium row has the wave now");
        } else {
            CHECK(false, "the Mexican wave tick box is drawn");
        }
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*st, e.rec, "hasmexicanwave") == 0, "undone: " + msg);
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*st, e.rec, "defaultweather") == 0, "undone: weather back to Clear: " + msg);
        app.sel_team = sel0;
        club_page_test::hide(ui);
    });

    run_case("club fields UI: kit extras on a kit row - written, read back, not kept (a kit is read again at every career load)", [&] {
        club_field_undo_clear();
        const Table* kt = app.db.table("teamkits");
        CHECK(kt && kt->field("jerseyfit") && kt->field("isgeneric"), "the test world has the kit extras");
        if (!kt || !kt->field("jerseyfit")) return;
        const uint64_t row = app.db.find(*kt, "teamkitid", 70);
        std::string msg;
        // earlier cases of a full run may have written this kit: take its values as they are and return to them
        const std::string kept0 = club_page_test::kept_rows(app.reapply);
        const int64_t fit0 = app.db.get_int(*kt, row, "jerseyfit"), shorts0 = app.db.get_int(*kt, row, "shortstyle");
        const int fit1 = fit0 == 0 ? 1 : 0;
        int shorts1 = 3;
        for (int v : {3, 2, 6, 7, 0})
            if (v != shorts0) { shorts1 = v; break; }
        CHECK(write_club_field(app, *kt, row, "jerseyfit", fit1, &msg) && app.db.get_int(*kt, row, "jerseyfit") == fit1 && club_field_undo_top_team() == 7,
              "fit changed: " + msg);
        CHECK(!write_club_field(app, *kt, row, "jerseyfit", 2, &msg) && app.db.get_int(*kt, row, "jerseyfit") == fit1, "Team Kit Fit (2) is outside FC 27: refused");
        CHECK(write_club_field(app, *kt, row, "shortstyle", shorts1, &msg) && app.db.get_int(*kt, row, "shortstyle") == shorts1, "shorts changed: " + msg);
        CHECK(!write_club_field(app, *kt, row, "shortstyle", 1, &msg), "shorts 1 (GK Pants) is not in the stock set: refused");
        CHECK(club_page_test::kept_rows(app.reapply) == kept0, "a kit extra is live only: no reapply row");
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*kt, row, "shortstyle") == shorts0, "undone: shorts back to where they were: " + msg);
        CHECK(undo_club_field(app, &msg) && app.db.get_int(*kt, row, "jerseyfit") == fit0, "undone: fit back to where it was: " + msg);
    });
}

}  // namespace club_fields_test
