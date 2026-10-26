// 3.2 f3 (brief 3.2-looks-onepicker, charter PITWALL.md 19.17): ONE picker component and ONE catalogue per look attribute.
//
// This file holds STATEMENTS, not functions: test_main.cpp includes it once, inside test_ui(), right after
// visual_picker_test::ui_cases(app, ui, le), so app / ui / le are in scope (the brief allows one include line in test_main.cpp;
// a header of functions would need a second line to call it). No #pragma once on purpose.
//
// How it works (a static site table checked against the source text; a registry the sites register into would have meant a
// call in every site, i.e. editing the pickers just to be able to count them):
//   1. SITES: every place that offers a choice for a look field, as (file, needle, kind, owner). A site is "present" when its
//      needle is in the file. A present site that is not held (owner "") must offer the SAME code set as the others.
//   2. HELD: a site with an owner (faces3d-proven, raw-entry, q6-nopicker, blocked-new-logic) is printed on every run with its
//      file:line and reason, and is not compared. An unlisted site fails (step 4).
//   3. CODES: for each field with several sites the code sets are compared (the game's list = gallery_codes is the catalogue,
//      brief Amendment A2; RDBM26 is a picture source only): the vp grid, the combo of item_galleries, the kit combo ...
//   4. COMPLETENESS: every statement of src/ui/*.cpp that names a look field and calls an option-offering widget must contain
//      the needle of a listed site of that file, else "unlisted picker site".
//   5. SELF-CHECK: a planted second catalogue must make the comparison fail, an identical one must pass.
{
    namespace opfs = std::filesystem;
    const opfs::path op_root = opfs::path(g_lua).parent_path().parent_path().parent_path();  // g_lua = <root>/tests/native/gui_world.lua
    const opfs::path op_ui = op_root / "src" / "ui";

    struct OpSite {
        const char* id;
        const char* file;    // under src/ui
        const char* needle;  // text that proves the site exists in that file
        const char* kind;    // vp | combo | free-int | own-gallery | own-popup | raw-grid | faces3d | shared-fn
        const char* fields;  // what it offers
        const char* owner;   // "" = compared; else the held list's owner
        const char* reason;
    };
    const std::vector<OpSite> op_sites = {
        // ---- the ONE component (ui_visual_picker.cpp) and the places that reuse it
        {"vp-grid", "ui_visual_picker.cpp", "bool build_source(", "vp", "every look field with a picture set or a gallery", "", ""},
        {"vp-inline", "ui_visual_picker.cpp", "bool inline_field(", "vp", "one inline row (name + Change)", "", ""},
        {"vp-inline-appearance", "ui_visual_picker.cpp", "void inline_appearance(", "vp", "brows, colours, eyes, skin, head type, accessory colours", "", ""},
        {"vp-body-list", "ui_visual_picker.cpp", "looks::body_grid_codes(&cat, current, gender, manager)", "shared-fn", "bodytypecode", "", ""},
        {"studio-body-card", "ui_looks_studio.cpp", "body_grid_codes(&cat, current, gender, manager)", "shared-fn", "bodytypecode", "", ""},
        {"studio-field-combo", "ui_looks_studio.cpp", "field_combo(app, s, t, rec, who, f.first", "vp", "generic-head hair / beard type and colour", "", ""},
        {"gallery-choose", "ui_images.cpp", "vp::open(app, t, rec, g.field, manager)", "vp", "hair, beard, boots, gloves, accessories", "", ""},
        // ---- duplicates that must draw from the one catalogue (compared while present)
        {"gallery-combo", "ui_images.cpp", "enum_editor(app, t, rec, *f, nullptr, S(240.0f));  // its name (", "combo", "hair, beard, boots, gloves, accessories", "", ""},
        {"kit-combo", "ui_images.cpp", "{\"sockstylecode\", \"Sock style\"}, {\"socklengthcode\", \"Sock length\"}", "combo", "socklengthcode, jerseysleevelengthcode as enum combos next to their vp rows", "", ""},
        // ---- held
        {"faces3d-gallery", "ui_faces3d_gate.cpp", "gallery_picker(app, \"", "faces3d", "hair / beard in the faces3d band (own codes 9999 / 511, base 8039 / 335)", "faces3d-proven",
         "Apply hair & beard is proven in game (CHANGELOG.md:115); its band codes are not the DB codes, writer not changed here"},
        {"raw-players-appearance", "ui_players.cpp", "\"##appgrid\"", "raw-grid", "every appearance field of a player, typed", "raw-entry", "typed raw code (Amendment A3), accepts what the gallery gate refuses"},
        {"raw-players-roles", "ui_players.cpp", "\"##rolegrid\"", "raw-grid", "bodytypecode typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-appearance", "ui_faces.cpp", "\"##mappgrid\"", "raw-grid", "manager hair, beard, eyes, skin, body, outfit typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-career", "ui_career_page.cpp", "\"##careermgrid\"", "raw-grid", "manager skintonecode typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-teams", "ui_teams.cpp", "\"##mgrid\"", "raw-grid", "manager bodytypecode / outfitid / skintonecode typed", "raw-entry", "typed raw code (A3)"},
        {"chooser-manager-eyes-skin", "ui_faces.cpp", "ImGui::Checkbox(\"Eyes\", &ch.opts.eyes)", "own-popup", "manager eye colour + skin copy boxes of the real-face chooser", "q6-nopicker",
         "Amendment B2: the manager card (Career / Teams > Appearance) has hair and beard galleries but no eye-colour or skin-tone picker"},
        {"tattoo-free-int", "ui_images.cpp", "field_editor(app, t, rec, *f, \"##tval\"", "free-int", "tattoohead ... tattoorightleg: typed number + its own popup (g_tattoos + catalog Tattoo family)", "blocked-new-logic",
         "the vp tattoo group offers the RDBM26 sets, the tattoo popup the save's tattoo table and the game catalog: one catalogue needs a decision and new logic (report)"},
        {"body-gallery", "ui_bodytypes.cpp", "c.filter(f)", "own-gallery", "bodytypecode: filters, copy from player, risky-pairing guard", "blocked-new-logic",
         "Catalog::filter lists the probed and named codes; body_grid_codes adds track F's list and drops the other gender: merging the two derivations is new logic (report)"},
        {"old-outfit-popup", "ui_images.cpp", "ImGui::OpenPopup(\"##gallerypick\")", "own-popup", "manager outfitid only (no vp set exists for it)", "blocked-new-logic",
         "the only picker of outfitid (not a duplicate); listed so the completeness scan knows it"},
    };

    auto op_read = [&](const char* file) {
        std::ifstream in(op_ui / file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    auto op_line = [](const std::string& s, size_t pos) { return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<long>(pos), '\n')); };

    // ---- the comparison itself: providers = (site name, code set); returns the problems, one per disagreeing site
    using OpProvider = std::pair<std::string, std::set<int64_t>>;
    auto op_diff = [](const std::set<int64_t>& a, const std::set<int64_t>& b) {
        std::string only_a, only_b;
        int na = 0, nb = 0;
        for (int64_t c : a)
            if (!b.count(c) && ++na <= 4) only_a += (only_a.empty() ? "" : ",") + std::to_string(c);
        for (int64_t c : b)
            if (!a.count(c) && ++nb <= 4) only_b += (only_b.empty() ? "" : ",") + std::to_string(c);
        return fmt("%d codes only in the first (%s), %d only in the second (%s)", na, only_a.c_str(), nb, only_b.c_str());
    };
    auto op_agree = [&](const std::string& field, const std::vector<OpProvider>& sites) {
        std::vector<std::string> problems;
        for (size_t i = 1; i < sites.size(); ++i)
            if (sites[i].second != sites[0].second)
                problems.push_back(field + ": " + sites[i].first + " (" + std::to_string(sites[i].second.size()) + " codes) differs from " + sites[0].first + " (" +
                                   std::to_string(sites[0].second.size()) + " codes): " + op_diff(sites[0].second, sites[i].second));
        return problems;
    };

    run_case("UI 3.2 one picker: every look field has ONE code set (the game's list) at every picker site; held sites listed; unlisted sites fail (charter 19.17)", [&] {
        const Table* p = app.db.table("players");
        const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
        CHECK(p && rec != 0, "the test world has player 2002");
        if (!p || !rec) return;

        // ---- 5. the self-check of the comparison: a planted second catalogue fails, an identical one passes
        {
            const std::set<int64_t> one = {3, 5, 8, 13};
            std::set<int64_t> two = one;
            two.insert(987654);  // a code only the planted catalogue has
            CHECK(op_agree("planted", {{"catalogue A", one}, {"catalogue B", two}}).size() == 1, "self-check: a planted second catalogue makes the comparison fail");
            CHECK(op_agree("planted", {{"catalogue A", one}, {"catalogue B", one}}).empty(), "self-check: the same catalogue twice passes");
        }

        // ---- 1/2. the site table against the source, and the held list (printed on every run)
        std::printf("  ONE-PICKER sites (file:line, kind, owner):\n");
        std::set<std::string> present;
        for (const OpSite& s : op_sites) {
            const std::string txt = op_read(s.file);
            const size_t pos = txt.find(s.needle);
            const std::string owner = s.owner[0] ? s.owner : "compared";
            if (pos == std::string::npos) {
                std::printf("    %-26s %-24s ABSENT (removed from the source)  [%s]\n", s.id, s.file, owner.c_str());
                continue;
            }
            present.insert(s.id);
            std::printf("    %-26s %s:%d  %s  [%s]%s%s\n", s.id, s.file, op_line(txt, pos), s.kind, owner.c_str(), s.reason[0] ? "  reason: " : "", s.reason);
        }
        CHECK(present.count("vp-grid") && present.count("studio-body-card") && present.count("raw-players-appearance") && present.count("faces3d-gallery"),
              "the anchors of the table are in the source (a renamed function must be updated here, not dropped)");
        // Amendment B3: no q6 owner; B2: q6-nopicker only for the managers' eyes and skin boxes
        for (const OpSite& s : op_sites) CHECK(std::string(s.owner) != "q6", std::string("no plain q6 owner (Amendment B3): ") + s.id);

        // ---- 4. completeness: a statement naming a look field and calling an option-offering widget must hold a listed needle of its file
        {
            static const char* const kLook[] = {"hairtypecode", "facialhairtypecode", "hairstylecode", "eyebrowcode", "haircolorcode", "facialhaircolorcode", "eyecolorcode", "eyedetail",
                                                "skintonecode", "skintypecode", "headtypecode", "headclasscode", "bodytypecode", "shoetypecode", "smallsidedshoetypecode",
                                                "gkglovetypecode", "accessorycode1", "accessorycode2", "accessorycode3", "accessorycode4", "accessorycolourcode1",
                                                "accessorycolourcode2", "accessorycolourcode3", "accessorycolourcode4", "tattoohead", "tattoofront", "tattooback",
                                                "tattooleftarm", "tattoorightarm", "tattooleftleg", "tattoorightleg", "outfitid"};
            static const char* const kOffer[] = {"enum_editor(", "field_editor(", "field_grid(", "code_value_list(", "inline_field(", "visual_field_row(", "look_combo(",
                                                 "field_combo(", "inline_grid(", "gallery_picker(", "vp::open("};
            std::vector<opfs::path> files;
            for (const auto& e : opfs::directory_iterator(op_ui))
                if (e.path().extension() == ".cpp") files.push_back(e.path());
            std::sort(files.begin(), files.end());
            int statements = 0, unlisted = 0;
            for (const opfs::path& f : files) {
                const std::string txt = op_read(f.filename().string().c_str());
                std::set<size_t> seen;
                for (const char* name : kLook) {
                    const std::string lit = std::string("\"") + name + "\"";
                    for (size_t at = txt.find(lit); at != std::string::npos; at = txt.find(lit, at + 1)) {
                        const size_t b = txt.rfind(';', at);
                        const size_t e = txt.find(';', at + lit.size());
                        const size_t st = b == std::string::npos ? 0 : b + 1;
                        if (e == std::string::npos || !seen.insert(st).second) continue;
                        const std::string stmt = txt.substr(st, e - st);
                        bool offers = false;
                        for (const char* o : kOffer) offers = offers || stmt.find(o) != std::string::npos;
                        if (!offers) continue;
                        ++statements;
                        bool listed = false;
                        for (const OpSite& s : op_sites) listed = listed || (f.filename() == s.file && stmt.find(s.needle) != std::string::npos);
                        if (!listed) {
                            ++unlisted;
                            CHECK(false, fmt("unlisted picker site for %s at %s:%d (add it to the table of test_one_picker.h, compared or held with an owner)", name,
                                             f.filename().string().c_str(), op_line(txt, at)));
                        }
                    }
                }
            }
            std::printf("  ONE-PICKER completeness: %d option-offering statements naming a look field, %d unlisted\n", statements, unlisted);
            CHECK(statements >= 6, fmt("the scan found the known statements (%d)", statements));
        }

        // ---- 3. the code sets. Fixture: the game's own list (legacy_filename_hash_list.csv) with a few ids per gallery directory
        const opfs::path hash = le / "legacy_filename_hash_list.csv";
        const std::string hash_before = opfs::exists(hash) ? read_file(hash) : std::string();
        {
            std::ofstream h(hash);
            h << "hash;file_name\n"
                 "1;data/ui/imgAssets/hairstyle/item_3261_0.dds\n2;data/ui/imgAssets/hairstyle/item_1053_0.dds\n"
                 "3;data/ui/imgAssets/facialhairstyle/item_11_0.dds\n4;data/ui/imgAssets/facialhairstyle/item_12_0.dds\n"
                 "5;data/ui/imgAssets/boots/item_16_0.dds\n6;data/ui/imgAssets/boots/item_17_0.dds\n"
                 "7;data/ui/imgAssets/gkglove/gkglove_5.dds\n8;data/ui/imgAssets/gkglove/gkglove_6.dds\n"
                 "9;data/ui/imgAssets/accessories/item_6_0.dds\n10;data/ui/imgAssets/accessories/item_7_0.dds\n";
        }
        app.request_tab = 0;
        ui.frames(70);  // gear_pictures looks at the lists once per ImGui second (60 frames): the new hash list must be read
        visual_pictures_override((test_tmp_dir("one_picker_none") / "nowhere").u8string());  // names only: the RDBM26 pictures are not the point
        visual_pictures_refresh(app);
        auto draw_section = [&app, p, rec]() {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
            if (ImGui::Begin("One picker test")) visual_pickers_section(app, *p, rec);
            ImGui::End();
        };
        ui.extra_draw = draw_section;
        ui.frames(2);

        auto strip = [&](std::set<int64_t> s, int64_t cur) {
            s.erase(0);
            s.erase(cur);
            return s;
        };
        auto vp_codes = [&](const std::string& field) {
            std::set<int64_t> out;
            visual_picker_close();
            ui.frames(2);
            vp::open(app, *p, rec, field, false);
            ui.frames(3);
            if (visual_picker_ui().open && visual_picker_ui().field == field) {
                const std::vector<int64_t> v = visual_picker_codes();
                out.insert(v.begin(), v.end());
            }
            visual_picker_close();
            ui.frames(2);
            return out;
        };
        const bool combo_present = present.count("gallery-combo") > 0;
        const bool kit_present = present.count("kit-combo") > 0;
        int compared = 0;
        // gallery fields: the vp grid, the combo next to it, and the game's list itself
        for (const char* field : {"hairtypecode", "facialhairtypecode", "shoetypecode", "smallsidedshoetypecode", "gkglovetypecode", "accessorycode1", "accessorycode2",
                                  "accessorycode3", "accessorycode4"}) {
            const Field* f = p->field(field);
            if (!f) continue;
            const int64_t cur = app.db.get_int(*p, rec, field, 0);
            const std::vector<int64_t> game = gallery_codes(app, field);
            std::vector<OpProvider> sites;
            sites.push_back({"the game's list (gallery_codes)", strip(std::set<int64_t>(game.begin(), game.end()), cur)});
            sites.push_back({"the vp grid (Choose... / Change)", strip(vp_codes(field), cur)});
            if (combo_present) {
                const std::vector<int64_t> cv = code_value_list(app, *p, *f, cur);
                sites.push_back({"the combo next to it (enum_editor / code_value_list)", strip(std::set<int64_t>(cv.begin(), cv.end()), cur)});
            }
            CHECK(!sites[0].second.empty() || std::string(field) == "smallsidedshoetypecode" || std::string(field).rfind("accessory", 0) == 0,
                  std::string("fixture: the game's list of ") + field + " is not empty");
            for (const std::string& pr : op_agree(field, sites)) CHECK(false, "ONE PICKER, ONE CATALOGUE: " + pr);
            ++compared;
        }
        // kit fit: the test world's players table has no sock / sleeve columns, so this cannot be compared by code sets: the statement
        // is the check. The "Socks and sleeves" list must not offer socklengthcode / jerseysleevelengthcode as enum combos beside their vp rows.
        if (kit_present) {
            CHECK(false, "ONE PICKER, ONE CATALOGUE: socklengthcode, jerseysleevelengthcode: the kit combo (enum_editor, in-use + named + range codes, "
                         "ui_images.cpp \"Socks and sleeves\") is a second catalogue next to the vp rows of visual_pickers_section (group Kit and boots)");
        }
        // body type: the vp list and the studio card are one function; the gallery is held but its difference is printed
        if (p->has("bodytypecode")) {
            const int64_t cur = app.db.get_int(*p, rec, "bodytypecode", 0);
            const int gender = p->has("gender") ? static_cast<int>(app.db.get_int(*p, rec, "gender", -1)) : -1;
            bodytype::Catalog& cat = bodytype_catalog(app);
            const std::vector<int64_t> shared = looks::body_grid_codes(&cat, cur, gender, false);
            const std::vector<int64_t> gal = cat.codes();
            std::vector<OpProvider> sites;
            sites.push_back({"looks::body_grid_codes (the studio card)", std::set<int64_t>(shared.begin(), shared.end())});
            sites.push_back({"the vp body list", vp_codes("bodytypecode")});
            for (const std::string& pr : op_agree("bodytypecode", sites)) CHECK(false, "ONE PICKER, ONE CATALOGUE: " + pr);
            const std::set<int64_t> g(gal.begin(), gal.end());
            std::printf("  ONE-PICKER held body-gallery (blocked-new-logic), for the report: %s\n", op_diff(sites[0].second, g).c_str());
            ++compared;
        }
        // tattoos: held, printed
        if (p->has("tattoohead")) {
            const std::set<int64_t> v = vp_codes("tattoohead");
            const std::vector<int64_t> gc = gallery_codes(app, "tattoohead");
            std::printf("  ONE-PICKER held tattoos (blocked-new-logic), for the report: vp group %zu codes, the tattoo popup's game list %zu codes\n", v.size(), gc.size());
        }
        CHECK(compared >= 6, fmt("fields compared (%d)", compared));
        std::printf("  ONE-PICKER compared %d fields; gallery combo %s, kit combo %s\n", compared, combo_present ? "PRESENT" : "gone", kit_present ? "PRESENT" : "gone");

        // ---- cleanup
        ui.extra_draw = nullptr;
        visual_pictures_override("");
        if (hash_before.empty()) {
            std::error_code ec;
            opfs::remove(hash, ec);
        } else {
            std::ofstream(hash, std::ios::binary | std::ios::trunc) << hash_before;
        }
        app.request_tab = 0;
        ui.frames(70);  // the lists are read again (the next case sees the file as it was)
    });
}
