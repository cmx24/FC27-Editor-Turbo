// 2.11 K1: named kit font / layout choices (core/kit_fonts.h, ui/ui_identity.cpp). Every kit row here is made by the test (the
// gui_world teamkits table); the game's names are the generated kit_fonts_data.h (asset NAMES only). Nothing touches a game folder.
#pragma once
#include <map>

#include "core/kit_fonts.h"
#include "core/kit_fonts_data.h"
#include "core/kit_import.h"
#include "core/reapply.h"

namespace kitfonts_test {
using namespace turbo;
using turbo::kitfonts::Kind;

static kitfonts::KitRow krow(int64_t team, int64_t type, int64_t name, int64_t num, int64_t shorts = -1, int64_t layout = -1) {
    kitfonts::KitRow r;
    r.team = team;
    r.type = type;
    r.value[0] = name;
    r.value[1] = num;
    r.value[2] = shorts;
    r.value[3] = layout;
    return r;
}
}  // namespace kitfonts_test

static void test_kit_fonts() {
    using namespace kitfonts_test;
    const fs::path dir = g_out / "kit_fonts";
    fs::create_directories(dir);

    run_case("kit fonts: the game's names (generated from its manifests), pretty names, the layout words", [&] {
        CHECK(kitfonts::game_name(Kind::NumberFont, 291) == "chelsea" && kitfonts::game_name(Kind::NameFont, 153) == "chelsea",
              "Chelsea's number font 291 and name font 153");
        CHECK(kitfonts::game_name(Kind::NumberFont, 144) == "nike" && kitfonts::game_name(Kind::NumberFont, 213) == "serie_a_shorts" &&
                  kitfonts::game_name(Kind::NameFont, 172) == "real_madrid",
              "other ids: " + kitfonts::game_name(Kind::NumberFont, 213));
        CHECK(kitfonts::game_name(Kind::ShortsNumberFont, 291) == "chelsea", "shorts numbers use the number fonts' names");
        CHECK(kitfonts::game_name(Kind::NumberFont, 15).empty() && kitfonts::game_name(Kind::NameFont, 9).empty() && kitfonts::game_name(Kind::NumberFont, -1).empty(),
              "an id the game does not name: no name");
        CHECK(kitfonts::game_name(Kind::NameLayout, 0) == "straight" && kitfonts::game_name(Kind::NameLayout, 1) == "arched" && kitfonts::game_name(Kind::NameLayout, 2).empty(),
              "layout 0 straight, 1 arched, others unnamed");
        CHECK(kitfonts::pretty_name("puma_2024") == "Puma 2024" && kitfonts::pretty_name("chelsea") == "Chelsea" && kitfonts::pretty_name("").empty(), "pretty names");
        // the generated tables: ascending, unique, as many as the manifest pass found (88 name fonts, 137 number fonts)
        auto sorted_unique = [](const auto& table, size_t n) {
            for (size_t i = 1; i < n; ++i)
                if (table[i - 1].id >= table[i].id) return false;
            return n > 0;
        };
        const size_t nf = std::size(kitfonts_data::kNameFonts), nn = std::size(kitfonts_data::kNumberFonts);
        CHECK(sorted_unique(kitfonts_data::kNameFonts, nf) && sorted_unique(kitfonts_data::kNumberFonts, nn) && nf == 88 && nn == 137,
              fmt("tables ascending and unique (%zu name fonts, %zu number fonts)", nf, nn));
        bool in_range = true;
        for (const auto& n : kitfonts_data::kNameFonts) in_range = in_range && n.id >= 0 && n.id <= kitfonts::schema_max(Kind::NameFont) && n.name[0];
        for (const auto& n : kitfonts_data::kNumberFonts) in_range = in_range && n.id >= 0 && n.id <= kitfonts::schema_max(Kind::NumberFont) && n.name[0];
        CHECK(in_range, "every id inside the 0..511 the schema allows, every name set");
        CHECK(std::string(kitfonts::field_of(Kind::NumberFont)) == "numberfonttype" && std::string(kitfonts::field_of(Kind::NameLayout)) == "jerseynamelayouttype", "field names");
    });

    run_case("kit fonts: worn-by index from a teamkits snapshot", [&] {
        const std::map<int64_t, std::string> names = {{1, "Arsenal"}, {95, "Leicester City"}, {100, "Chelsea"}, {101, "Brighton"}};
        std::vector<kitfonts::KitRow> rows = {
            krow(100, 0, 153, 291, 291, 1),  // Chelsea home
            krow(100, 1, 153, 291, 291, 1),  // Chelsea away
            krow(1, 0, 18, 291, 95, 0),      // Arsenal home wears Chelsea's number font
            krow(95, 0, 7, 144, 144, 0),     // Leicester home
            krow(95, 2, 7, 15, -1, -1),      // Leicester goalkeeper: number font 15 (the game names none), shorts unreadable
            krow(100, 5, 153, 292, 292, 1),  // referee kit: not counted
            krow(100, 7, 153, 292, 292, 1),  // training kit: not counted
            krow(999, 0, 18, 144, 144, 0),   // a club the name function does not know
            krow(101, 3, 18, 144, -1, 0),    // Brighton third
        };
        kitfonts::Index idx;
        CHECK(idx.empty(), "empty before");
        idx.build(rows, [&](int64_t t) {
            auto it = names.find(t);
            return it == names.end() ? std::string() : it->second;
        });
        CHECK(!idx.empty() && idx.kits() == 7, fmt("7 kits of the types 0..4 counted (%d)", idx.kits()));
        const kitfonts::Entry* e = idx.find(Kind::NumberFont, 291);
        CHECK(e && e->kits == 3 && e->clubs.size() == 2 && e->clubs[0].club == "Chelsea" && e->clubs[0].kits == 2 && e->clubs[1].club == "Arsenal",
              "number font 291: Chelsea (2 kits) first, then Arsenal");
        e = idx.find(Kind::NumberFont, 144);
        CHECK(e && e->kits == 3 && e->clubs.size() == 3, "144 worn by three clubs");
        CHECK(e && e->clubs[0].kits == 1 && e->clubs[0].club == "Brighton" && e->clubs[1].club == "Leicester City" && e->clubs[2].club == "team 999",
              "same kit count: by name; the unknown club is 'team 999'");
        CHECK(idx.find(Kind::NumberFont, 292) == nullptr && idx.find(Kind::NumberFont, 5) == nullptr, "referee and training kits are not counted");
        CHECK(idx.find(Kind::NameFont, 153) && idx.find(Kind::NameFont, 153)->kits == 2, "name font 153 worn by two kits");
        CHECK(idx.find(Kind::ShortsNumberFont, 95) && idx.find(Kind::ShortsNumberFont, 95)->clubs[0].club == "Arsenal", "shorts number font: Arsenal's 95");
        CHECK(idx.find(Kind::NameLayout, 1)->kits == 2 && idx.find(Kind::NameLayout, 0)->kits == 4, "layouts counted; -1 (unreadable) is skipped");
        CHECK(kitfonts::worn_by(idx.find(Kind::NumberFont, 291)) == "worn by 2 clubs: Chelsea, Arsenal", "worn_by: " + kitfonts::worn_by(idx.find(Kind::NumberFont, 291)));
        CHECK(kitfonts::worn_by(idx.find(Kind::NumberFont, 144), 2) == "worn by 3 clubs: Brighton, Leicester City and 1 more", "worn_by cut: " + kitfonts::worn_by(idx.find(Kind::NumberFont, 144), 2));
        CHECK(kitfonts::worn_by(nullptr) == "worn by no club in this database", "worn_by nobody");
        CHECK(kitfonts::choice_label(Kind::NumberFont, 291, idx.find(Kind::NumberFont, 291)) == "291  Chelsea  -  worn by Chelsea, Arsenal", "label");
        CHECK(kitfonts::choice_label(Kind::NumberFont, 15, idx.find(Kind::NumberFont, 15)) == "15  (not named by the game)  -  worn by Leicester City", "an unnamed id still lists its clubs");
        CHECK(kitfonts::choice_label(Kind::NumberFont, 144, idx.find(Kind::NumberFont, 144)) == "144  Nike  -  worn by Brighton, Leicester City, team 999", "label with three clubs");
        CHECK(kitfonts::choice_label(Kind::NumberFont, 2, nullptr) == "2  Generic  -  worn by no club", "a named id nobody wears");
        CHECK(kitfonts::short_label(Kind::NumberFont, 291) == "291  Chelsea" && kitfonts::short_label(Kind::NumberFont, 15) == "15", "closed combo text");
        // the filter: every word in the id, the game's name or a club
        e = idx.find(Kind::NumberFont, 291);
        CHECK(kitfonts::matches(Kind::NumberFont, 291, e, "") && kitfonts::matches(Kind::NumberFont, 291, e, "chelsea") && kitfonts::matches(Kind::NumberFont, 291, e, "ARSENAL") &&
                  kitfonts::matches(Kind::NumberFont, 291, e, "291") && kitfonts::matches(Kind::NumberFont, 291, e, "  chel   ars ") && !kitfonts::matches(Kind::NumberFont, 291, e, "chelsea nike"),
              "filter: words, case, id, club names");
        CHECK(!kitfonts::matches(Kind::NumberFont, 144, idx.find(Kind::NumberFont, 144), "chelsea") && kitfonts::matches(Kind::NumberFont, 144, idx.find(Kind::NumberFont, 144), "leicester"), "filter: other ids");
        // the picker's ids: worn ones plus the game's named ones, ascending, no duplicates, the unnamed worn id 15 included
        const std::vector<int64_t> ids = kitfonts::choice_ids(Kind::NumberFont, idx);
        CHECK(ids.size() >= 137 && std::is_sorted(ids.begin(), ids.end()) && std::adjacent_find(ids.begin(), ids.end()) == ids.end() &&
                  std::find(ids.begin(), ids.end(), 15) != ids.end() && std::find(ids.begin(), ids.end(), 291) != ids.end() && std::find(ids.begin(), ids.end(), 0) != ids.end(),
              fmt("choice ids (%zu)", ids.size()));
        CHECK(kitfonts::choice_ids(Kind::NameLayout, idx).size() == 2 && kitfonts::choice_ids(Kind::NameFont, kitfonts::Index{}).size() == 88, "layout 0 and 1; name fonts without any wearer: the game's 88");
        idx.build({}, nullptr);
        CHECK(idx.empty() && idx.entries(Kind::NumberFont).empty(), "rebuilt from nothing");
    });

    run_case("kit fonts: kept for every career load; the kit import's keys and range are unchanged", [&] {
        for (const char* f : {"jerseynamefonttype", "numberfonttype", "shortsnumberfonttype", "jerseynameoutlinewidth"})
            CHECK(kit_font_field(f) && kit_setting_field(f) && kit_kept_field(f) && !kit_colour_field(f), std::string(f) + " is kept like a setting");
        CHECK(kit_kept_field("jerseynamelayouttype") && !kit_font_field("jerseynamelayouttype"), "the layout stays one of the import's settings");
        CHECK(!kit_kept_field("teamtechid") && !kit_kept_field("teamkitid") && !kit_kept_field("jerseyfit") && !kit_font_field("teamcolorprimr"), "no key column, no other field");
        CHECK(kit_setting_fields().size() == 6 && std::find(kit_setting_fields().begin(), kit_setting_fields().end(), "numberfonttype") == kit_setting_fields().end(),
              "the import's own list is still its 6 keys, without the fonts");
        // a settings.json with font keys: the import still lists them as ignored (it never wrote them), the known keys parse as before
        kits::KitSettings ks;
        std::string err;
        CHECK(kits::parse_settings(R"({"teamcolorprimpercent": "100", "numberfonttype": 291, "jerseynamefonttype": 153, "jerseynamelayouttype": 1})", ks, &err) &&
                  ks.fields.size() == 2 && ks.ignored.size() == 2,
              "settings.json: 2 known keys written, 2 font keys ignored: " + err);
        // the store keeps the fonts, drops a key column
        ReapplyStore st;
        st.set_kit_field(95, 0, 70, "Leicester City", "2026-10-16 10:00", "numberfonttype", 291);
        st.set_kit_field(95, 0, 70, "", "", "jerseynamefonttype", 153);
        st.set_kit_field(95, 0, 70, "", "", "teamcolorprimr", 12);
        const fs::path f = dir / "reapply_edits.json";
        CHECK(save_reapply_store(f, st, &err, false), "saved: " + err);
        ReapplyStore back;
        size_t dropped = 0;
        CHECK(load_reapply_store(f, back, &err, &dropped) && dropped == 0 && back.kit(95, 0, 70) && back.kit(95, 0, 70)->fields.size() == 3 &&
                  back.kit(95, 0, 70)->fields.at("numberfonttype") == 291 && back.kit(95, 0, 70)->fields.at("jerseynamefonttype") == 153,
              "read back with the fonts: " + err);
        std::string text;
        {
            std::ifstream in(f, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const size_t at = text.find("\"numberfonttype\"");
        CHECK(at != std::string::npos, "the file names the field");
        text.replace(at, 16, "\"teamkitid\"");
        std::ofstream(f, std::ios::binary | std::ios::trunc) << text;
        ReapplyStore hand;
        CHECK(load_reapply_store(f, hand, &err, &dropped) && dropped == 1 && hand.kit(95, 0, 70)->fields.count("teamkitid") == 0 && hand.kit(95, 0, 70)->fields.size() == 2,
              "a hand-edited key column is dropped, the rest is kept");
    });
}

// UI: Teams > Colours (inside test_ui, after the colours case): the named pickers write, keep and survive a career load
static void kit_fonts_ui_cases(App& app, Ui& ui, const fs::path& le) {
    using namespace kitfonts_test;
    run_case("UI: Teams > Colours: kit font pickers by name and club; written, kept, written again at a career load", [&] {
        const Table* kt = app.db.table("teamkits");
        CHECK(kt && kt->field("numberfonttype") && kt->field("jerseynamefonttype"), "the test world has the kit font fields");
        const uint64_t row = app.db.find(*kt, "teamkitid", 70);
        const uint64_t ars = app.db.find(*kt, "teamkitid", 10);
        ui.frames(2);
        parity_test::scroll_into_view(ui, "Home kit");  // 2.20 K3: the crowd and stadium sections above the kits push them down the tab
        CHECK(ui.find("##kf", "##tcolours", "numberfonttype") && ui.find("##kf", "##tcolours", "jerseynamefonttype") && ui.find("##kf", "##tcolours", "shortsnumberfonttype") &&
                  ui.find("##kf", "##tcolours", "jerseynamelayouttype"),
              "four named pickers on the kit");
        CHECK(ui.find("##ow", "##tcolours", "jerseynameoutlinewidth") != nullptr, "the outline width slider");
        // the index: Arsenal's two kits wear 291 (the number font the game names "chelsea"), Everton's home 144
        const kitfonts::Index& idx = kit_font_index(app);
        CHECK(idx.find(Kind::NumberFont, 291) && idx.find(Kind::NumberFont, 291)->kits == 2 && idx.find(Kind::NumberFont, 291)->clubs.size() == 1 &&
                  idx.find(Kind::NumberFont, 291)->clubs[0].club == app.model.team_name(1) && idx.find(Kind::NumberFont, 144) &&
                  idx.find(Kind::NumberFont, 144)->clubs[0].club == app.model.team_name(7),
              "the live teamkits give the wearers");
        CHECK(app.db.get_int(*kt, row, "numberfonttype") == 144 && app.reapply.kit(7, 0, 70) == nullptr, "Everton wears 144; nothing kept");
        // open the number font combo, filter on the club, pick 291
        CHECK(ui.click("##kf", "##tcolours", "numberfonttype"), "combo opened");
        ui.frames(2);
        const ItemRec* filter = ui.find("##kff");
        CHECK(filter != nullptr, "filter box in the popup");
        if (filter) {
            ui.click(filter);
            ImGui::GetIO().AddInputCharactersUTF8(app.model.team_name(1).c_str());
            ui.frames(3);
        }
        const std::string want = kitfonts::choice_label(Kind::NumberFont, 291, idx.find(Kind::NumberFont, 291));
        CHECK(ui.find(want) != nullptr, "the club filter leaves '" + want + "'");
        CHECK(ui.find(kitfonts::choice_label(Kind::NumberFont, 2, idx.find(Kind::NumberFont, 2))) == nullptr, "a font of no wearer is filtered out");
        CHECK(ui.click(want), "picked");
        ui.frames(3);
        CHECK(app.db.get_int(*kt, row, "numberfonttype") == 291, fmt("Everton's number font is 291 now (%lld)", static_cast<long long>(app.db.get_int(*kt, row, "numberfonttype"))));
        CHECK(app.db.get_int(*kt, row, "shortsnumberfonttype") == 144 && app.db.get_int(*kt, row, "jerseynamefonttype") == 7, "the other fonts are untouched");
        const KitEdit* k = app.reapply.kit(7, 0, 70);
        CHECK(k && k->fields.size() == 1 && k->fields.at("numberfonttype") == 291, "kept like a colour: through the same store");
        CHECK(kit_font_index(app).find(Kind::NumberFont, 291)->clubs.size() == 2 && kit_font_index(app).find(Kind::NumberFont, 144) == nullptr,
              "the wearers follow the edit at once");
        CHECK(ui.find("Forget##kitkeep", "##tcolours") != nullptr, "the kept-edits line with Forget is shown");
        // writer rules
        std::string msg;
        CHECK(!write_kit_font(app, *kt, row, "numberfonttype", 512, &msg) && !msg.empty() && app.db.get_int(*kt, row, "numberfonttype") == 291, "512 is refused by the field range: " + msg);
        CHECK(!write_kit_font(app, *kt, row, "teamcolorprimr", 5, &msg) && msg.find("not a kit font field") != std::string::npos, "any other field is refused: " + msg);
        CHECK(!write_kit_font(app, *kt, row, "nosuchfield", 5, &msg), "an unknown field is refused");
        CHECK(write_kit_font(app, *kt, row, "jerseynamelayouttype", 1, &msg) && app.db.get_int(*kt, row, "jerseynamelayouttype") == 1 &&
                  app.reapply.kit(7, 0, 70)->fields.at("jerseynamelayouttype") == 1,
              "the layout: written and kept: " + msg);
        CHECK(write_kit_font(app, *kt, row, "jerseynameoutlinewidth", 3, &msg) && write_kit_font(app, *kt, row, "jerseynamefonttype", 153, &msg), "outline width and name font written");
        CHECK(app.reapply.kit(7, 0, 70)->fields.size() == 4, "four fields kept for Everton's home kit");
        // FC 27 reloads teamkits at a career load: the old values come back, Turbo writes the kept ones again
        CHECK(app.db.set_int(*kt, row, "numberfonttype", 144) && app.db.set_int(*kt, row, "jerseynamefonttype", 7) && app.db.set_int(*kt, row, "jerseynameoutlinewidth", 0) &&
                  app.db.set_int(*kt, row, "jerseynamelayouttype", 0),
              "the game's own values back");
        // the re-apply bumps App::gen (cached views reload): put it back so the cases after this one see the state they were written for
        const int gen_before = app.gen;
        const std::string line = app.reapply_stored_edits();
        CHECK(app.gen != gen_before, "the re-apply asks the cached views to reload");
        app.gen = gen_before;
        app.toasts.clear();
        app.reapply_status.clear();
        CHECK(app.db.get_int(*kt, row, "numberfonttype") == 291 && app.db.get_int(*kt, row, "jerseynamefonttype") == 153 && app.db.get_int(*kt, row, "jerseynameoutlinewidth") == 3 &&
                  app.db.get_int(*kt, row, "jerseynamelayouttype") == 1,
              "written again: " + line);
        CHECK(line.find("4 kit settings") != std::string::npos, "the summary counts them as settings: " + line);
        CHECK(app.db.get_int(*kt, ars, "numberfonttype") == 291, "Arsenal's own kit was never touched");
        // Forget (never leave a row behind: the 2.6 lesson), the game's values restored in the test world
        CHECK(ui.click("Forget##kitkeep", "##tcolours"), "Forget");
        CHECK(app.reapply.empty(), "forgotten: the store is empty again");
        CHECK(app.db.set_int(*kt, row, "numberfonttype", 144) && app.db.set_int(*kt, row, "jerseynamefonttype", 7) && app.db.set_int(*kt, row, "jerseynameoutlinewidth", 0) &&
                  app.db.set_int(*kt, row, "jerseynamelayouttype", 0),
              "the test world restored");
        ReapplyStore on_disk;
        std::string err;
        CHECK(load_reapply_store(reapply_store_path(le), on_disk, &err) && on_disk.empty(), "the store on disk is empty: " + err);
    });
}
