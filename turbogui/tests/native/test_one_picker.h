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
//   3. CODES (superset rule, fix round 1 of 3.2): for each field the ONE picker (the vp grid) must offer a SUPERSET of what the
//      field could be picked or written with on the base 9a1e888: the game's list (gallery_codes, the proven catalogue, Amendment A2),
//      the codes the old combo offered (code_value_list) and the RDBM26 set (picture and name source, which release users who have
//      no asset catalogue still pick from). Never equality: a code that only the RDBM26 set knows is a capability, not a second catalogue.
//      The cases: catalogue off (players, managers), catalogue active (the grid's WRITABLE set), and the Simple player page with no
//      RDBM26 folder (the Eyes / Skin boxes of the real-face chooser stay while no picker gives them).
//   4. COMPLETENESS: every statement of src/ui/*.cpp that names a look field and calls an option-offering widget must contain
//      the needle of a listed site of that file, else "unlisted picker site".
//   5. SELF-CHECK: a planted second catalogue must make the comparison fail, an identical one must pass; a planted missing code
//      must make the superset check fail.
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
        {"kit-inline", "ui_images.cpp", "vp::inline_field(app, t, rec, k[0], k[1])", "vp", "socklengthcode, jerseysleevelengthcode (Socks and sleeves)", "", ""},
        // ---- duplicates that must draw from the one catalogue (compared while present)
        {"gallery-combo", "ui_images.cpp", "enum_editor(app, t, rec, *f, nullptr, S(240.0f));  // its name (", "combo", "hair, beard, boots, gloves, accessories", "", ""},
        {"kit-combo", "ui_images.cpp", "{\"sockstylecode\", \"Sock style\"}, {\"socklengthcode\"", "combo", "socklengthcode, jerseysleevelengthcode as enum combos next to their vp rows", "", ""},
        // ---- held
        {"faces3d-gallery", "ui_faces3d_gate.cpp", "gallery_picker(app, \"", "faces3d", "hair / beard in the faces3d band (own codes 9999 / 511, base 8039 / 335)", "faces3d-proven",
         "Apply hair & beard is proven in game (CHANGELOG.md:115); its band codes are not the DB codes, writer not changed here"},
        {"raw-players-appearance", "ui_players.cpp", "\"##appgrid\"", "raw-grid", "every appearance field of a player, typed", "raw-entry", "typed raw code (Amendment A3), accepts what the gallery gate refuses"},
        {"raw-players-roles", "ui_players.cpp", "\"##rolegrid\"", "raw-grid", "bodytypecode typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-appearance", "ui_faces.cpp", "\"##mappgrid\"", "raw-grid", "manager hair, beard, eyes, skin, body, outfit typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-career", "ui_career_page.cpp", "\"##careermgrid\"", "raw-grid", "manager skintonecode typed", "raw-entry", "typed raw code (A3)"},
        {"raw-manager-teams", "ui_teams.cpp", "\"##mgrid\"", "raw-grid", "manager bodytypecode / outfitid / skintonecode typed", "raw-entry", "typed raw code (A3)"},
        {"chooser-manager-eyes-skin", "ui_faces.cpp", "ImGui::Checkbox(\"Eyes\", &ch.opts.eyes)", "own-popup", "manager eye colour + skin copy boxes of the real-face chooser", "q6-nopicker",
         "Amendment B2: the manager card (Career / Teams > Appearance) has hair and beard galleries but no eye-colour or skin-tone picker; "
         "the player's two boxes stay too while the card has no picture source (no RDBM26 folder), see the Simple-page case"},
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
    // superset rule: every code of `need` is in `have`; returns the problem (empty = ok)
    auto op_missing = [&](const std::string& what, const std::set<int64_t>& need, const std::set<int64_t>& have) {
        std::string first;
        int n = 0;
        for (int64_t c : need)
            if (!have.count(c) && ++n <= 6) first += (first.empty() ? "" : ",") + std::to_string(c);
        return n == 0 ? std::string() : fmt("%s: %d of %zu codes are missing from the one picker (first: %s)", what.c_str(), n, need.size(), first.c_str());
    };
    auto op_strip = [](std::set<int64_t> s, int64_t cur) {
        s.erase(0);
        s.erase(cur);
        return s;
    };
    // the RDBM26 set of a field (names and codes compiled in; the pictures are the user's own folder)
    auto op_rdbm = [](const std::string& field) {
        std::set<int64_t> out;
        if (const turbo::visual::SetDef* d = turbo::visual::set_for_field(field)) {
            const turbo::visual::CodeDef* b = turbo::visual::codes_begin(*d);
            for (int i = 0; i < d->count; ++i) out.insert(b[i].code);
        }
        return out;
    };
    // the one picker's list of a field: codes in list order, and which of them a tile can write (the catalogue gate)
    struct OpVp {
        bool open = false;
        std::vector<int64_t> codes;
        std::set<int64_t> writable;
        std::set<int64_t> all() const { return std::set<int64_t>(codes.begin(), codes.end()); }
        bool unique() const { return all().size() == codes.size(); }
    };
    auto op_vp = [&](const Table& t, uint64_t r, const std::string& field, bool manager) {
        OpVp o;
        visual_picker_close();
        ui.frames(2);
        vp::open(app, t, r, field, manager);
        ui.frames(3);
        if (visual_picker_ui().open && visual_picker_ui().field == field) {
            o.open = true;
            o.codes = visual_picker_codes();
            for (int64_t c : o.codes)
                if (visual_picker_tile_writable(c)) o.writable.insert(c);
        }
        visual_picker_close();
        ui.frames(2);
        return o;
    };
    auto op_draw = [&](const Table* t, uint64_t r) {
        ui.extra_draw = [&app, t, r]() {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
            if (ImGui::Begin("One picker test")) visual_pickers_section(app, *t, r);
            ImGui::End();
        };
        ui.frames(2);
    };
    // fixture: the game's own list (legacy_filename_hash_list.csv) with a few ids per gallery directory; no RDBM26 folder
    const opfs::path op_hash = le / "legacy_filename_hash_list.csv";
    auto op_fixture_on = [&]() {
        const std::string before = opfs::exists(op_hash) ? read_file(op_hash) : std::string();
        {
            std::ofstream h(op_hash);
            h << "hash;file_name\n"
                 "1;data/ui/imgAssets/hairstyle/item_3261_0.dds\n2;data/ui/imgAssets/hairstyle/item_1053_0.dds\n"
                 "3;data/ui/imgAssets/facialhairstyle/item_11_0.dds\n4;data/ui/imgAssets/facialhairstyle/item_12_0.dds\n"
                 "5;data/ui/imgAssets/boots/item_16_0.dds\n6;data/ui/imgAssets/boots/item_17_0.dds\n"
                 "7;data/ui/imgAssets/gkglove/gkglove_5.dds\n8;data/ui/imgAssets/gkglove/gkglove_6.dds\n"
                 "9;data/ui/imgAssets/accessories/item_6_0.dds\n10;data/ui/imgAssets/accessories/item_7_0.dds\n";
        }
        app.request_tab = 0;
        ui.frames(70);  // gear_pictures looks at the lists once per ImGui second (60 frames): the new hash list must be read
        visual_pictures_override((test_tmp_dir("one_picker_none") / "nowhere").u8string());  // names only: no RDBM26 folder (release users have none)
        visual_pictures_refresh(app);
        return before;
    };
    auto op_fixture_off = [&](const std::string& before) {
        ui.extra_draw = nullptr;
        visual_pictures_override("");
        if (before.empty()) {
            std::error_code ec;
            opfs::remove(op_hash, ec);
        } else {
            std::ofstream(op_hash, std::ios::binary | std::ios::trunc) << before;
        }
        app.request_tab = 0;
        ui.frames(70);  // the lists are read again (the next case sees the file as it was)
    };
    std::set<std::string> op_present;
    for (const OpSite& st : op_sites)
        if (op_read(st.file).find(st.needle) != std::string::npos) op_present.insert(st.id);
    static const char* const kOpGallery[] = {"hairtypecode", "facialhairtypecode", "shoetypecode", "smallsidedshoetypecode", "gkglovetypecode",
                                             "accessorycode1", "accessorycode2", "accessorycode3", "accessorycode4"};

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
            CHECK(!op_missing("planted", two, one).empty(), "self-check: a code the picker lacks makes the superset check fail");
            CHECK(op_missing("planted", one, two).empty() && op_missing("planted", one, one).empty(), "self-check: a superset passes");
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

        // ---- 3. the kit rule and the shared body list are checked in the cases below
        const bool kit_present = op_present.count("kit-combo") > 0;
        CHECK(!op_present.count("gallery-combo"), "ONE PICKER, ONE CATALOGUE: the gallery's code combo (enum_editor) is gone from item_galleries; the picker is the only chooser");
        if (kit_present) {
            CHECK(false, "ONE PICKER, ONE CATALOGUE: socklengthcode, jerseysleevelengthcode: the kit combo (enum_editor, in-use + named + range codes, "
                         "ui_images.cpp \"Socks and sleeves\") is a second catalogue next to the vp rows of visual_pickers_section (group Kit and boots)");
        }
        CHECK(kit_present || op_present.count("kit-inline"), "the sock length / sleeves rows are inline rows of the one picker (vp::inline_field) once the kit combo is gone");
    });

    // ---- catalogue off (every release user): players
    run_case("UI 3.2 one picker (fix 1): catalogue off, players: the ONE picker offers a SUPERSET of the game's list, the old combo's list and the RDBM26 set, merged once", [&] {
        const Table* p = app.db.table("players");
        const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
        CHECK(p && rec != 0, "the test world has player 2002");
        if (!p || !rec) return;
        set_gear_catalog(nullptr);
        const std::string before = op_fixture_on();
        op_draw(p, rec);
        int compared = 0;
        for (const char* field : kOpGallery) {
            const Field* f = p->field(field);
            if (!f) continue;
            const int64_t cur = app.db.get_int(*p, rec, field, 0);
            const OpVp v = op_vp(*p, rec, field, false);
            CHECK(v.open, std::string("the picker of ") + field + " opens");
            CHECK(v.unique(), std::string("ONE merged list, no code twice: ") + field);
            const std::set<int64_t> have = op_strip(v.all(), cur);
            const std::vector<int64_t> game = gallery_codes(app, field);
            const std::vector<int64_t> cv = code_value_list(app, *p, *f, cur);
            CHECK(!game.empty() || std::string(field) == "smallsidedshoetypecode" || std::string(field).rfind("accessory", 0) == 0,
                  std::string("fixture: the game's list of ") + field + " is not empty");
            for (const std::string& pr : {op_missing(std::string("ONE PICKER NO LOST CAPABILITY ") + field + " vs the game's list", op_strip(std::set<int64_t>(game.begin(), game.end()), cur), have),
                                          op_missing(std::string("ONE PICKER NO LOST CAPABILITY ") + field + " vs the old combo's list", op_strip(std::set<int64_t>(cv.begin(), cv.end()), cur), have),
                                          op_missing(std::string("ONE PICKER NO LOST CAPABILITY ") + field + " vs the RDBM26 set", op_strip(op_rdbm(field), cur), have)})
                CHECK(pr.empty(), pr);
            ++compared;
        }
        CHECK(compared >= 5, fmt("fields compared (%d)", compared));
        // the literal codes of 9a1e888's picker (visual_names_data.h): accessories, goalkeeper gloves, boots. They have models in the
        // game, a release user has no catalogue to say so, and the grid listed them: they stay pickable
        struct Lit {
            const char* field;
            std::vector<int64_t> codes;
        };
        const std::vector<int64_t> gloves = {105, 106, 107, 108, 110, 113, 114, 115, 117, 118, 119, 120, 121, 122, 123};
        for (const Lit& l : std::vector<Lit>{{"accessorycode1", {6, 8, 10, 12, 14, 22, 24, 26, 44, 190}},
                                             {"gkglovetypecode", gloves},
                                             {"shoetypecode", {240, 241, 242}}}) {
            CHECK(p->field(l.field) != nullptr, std::string("fixture: the players table has ") + l.field);
            if (!p->field(l.field)) continue;
            const int64_t cur = app.db.get_int(*p, rec, l.field, 0);
            const std::set<int64_t> have = op_vp(*p, rec, l.field, false).all();
            std::set<int64_t> need(l.codes.begin(), l.codes.end());
            need.erase(cur);
            const std::string pr = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (literal codes) ") + l.field, need, have);
            CHECK(pr.empty(), pr);
        }
        // the body type list: the vp list and the studio card are one function (the gallery is held, its difference is printed)
        if (p->has("bodytypecode")) {
            const int64_t cur = app.db.get_int(*p, rec, "bodytypecode", 0);
            const int gender = p->has("gender") ? static_cast<int>(app.db.get_int(*p, rec, "gender", -1)) : -1;
            bodytype::Catalog& cat = bodytype_catalog(app);
            const std::vector<int64_t> shared = looks::body_grid_codes(&cat, cur, gender, false);
            const std::vector<int64_t> gal = cat.codes();
            std::vector<OpProvider> sites;
            sites.push_back({"looks::body_grid_codes (the studio card)", std::set<int64_t>(shared.begin(), shared.end())});
            sites.push_back({"the vp body list", op_vp(*p, rec, "bodytypecode", false).all()});
            for (const std::string& pr : op_agree("bodytypecode", sites)) CHECK(false, "ONE PICKER, ONE CATALOGUE: " + pr);
            const std::set<int64_t> g(gal.begin(), gal.end());
            std::printf("  ONE-PICKER held body-gallery (blocked-new-logic), for the report: %s\n", op_diff(sites[0].second, g).c_str());
        }
        // tattoos: held, printed
        if (p->has("tattoohead")) {
            const OpVp v = op_vp(*p, rec, "tattoohead", false);
            const std::vector<int64_t> gc = gallery_codes(app, "tattoohead");
            std::printf("  ONE-PICKER held tattoos (blocked-new-logic), for the report: vp group %zu codes, the tattoo popup's game list %zu codes\n", v.codes.size(), gc.size());
        }
        std::printf("  ONE-PICKER catalogue off, players: %d fields compared as supersets\n", compared);
        op_fixture_off(before);
    });

    // ---- catalogue off: managers (the manager view is never gated, there is no catalogue for it)
    run_case("UI 3.2 one picker (fix 1): catalogue off, managers: hair and beard offer a SUPERSET of the old combo's list, the game's list and the RDBM26 set", [&] {
        const Table* mt = app.db.table("manager");
        const auto& mgrs = app.model.managers();
        CHECK(mt && !mgrs.empty(), "the test world has a manager");
        if (!mt || mgrs.empty()) return;
        const uint64_t rec = mgrs[0].rec;
        set_gear_catalog(nullptr);
        const std::string before = op_fixture_on();
        op_draw(mt, rec);
        int compared = 0;
        for (const char* field : {"hairtypecode", "facialhairtypecode"}) {
            const Field* f = mt->field(field);
            CHECK(f != nullptr, std::string("fixture: the manager table has ") + field);
            if (!f) continue;
            const int64_t cur = app.db.get_int(*mt, rec, field, 0);
            const OpVp v = op_vp(*mt, rec, field, true);
            CHECK(v.open, std::string("the manager picker of ") + field + " opens");
            CHECK(v.unique(), std::string("ONE merged list, no code twice (manager): ") + field);
            const std::set<int64_t> have = op_strip(v.all(), cur);
            const std::vector<int64_t> game = gallery_codes(app, field);
            const std::vector<int64_t> cv = code_value_list(app, *mt, *f, cur);
            for (const std::string& pr : {op_missing(std::string("ONE PICKER NO LOST CAPABILITY (manager) ") + field + " vs the game's list", op_strip(std::set<int64_t>(game.begin(), game.end()), cur), have),
                                          op_missing(std::string("ONE PICKER NO LOST CAPABILITY (manager) ") + field + " vs the old combo's list", op_strip(std::set<int64_t>(cv.begin(), cv.end()), cur), have),
                                          op_missing(std::string("ONE PICKER NO LOST CAPABILITY (manager) ") + field + " vs the RDBM26 set", op_strip(op_rdbm(field), cur), have)})
                CHECK(pr.empty(), pr);
            ++compared;
        }
        CHECK(compared == 2, fmt("manager fields compared (%d)", compared));
        op_fixture_off(before);
    });

    // ---- catalogue active (a developer with the asset catalogue): what a tile can WRITE is a superset of what the gate lets through
    run_case("UI 3.2 one picker (fix 1): catalogue active, players: every code the gate lets through is a writable tile; the codes it refuses are still listed, greyed", [&] {
        const Table* p = app.db.table("players");
        const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
        CHECK(p && rec != 0, "the test world has player 2002");
        if (!p || !rec) return;
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(), "catalog fixture loaded: " + cerr);
        const std::string before = op_fixture_on();
        set_gear_catalog(cat);
        op_draw(p, rec);
        int compared = 0, gated = 0;
        for (const char* field : {"hairtypecode", "facialhairtypecode", "shoetypecode", "gkglovetypecode", "accessorycode1"}) {
            const Field* f = p->field(field);
            CHECK(f != nullptr, std::string("fixture: the players table has ") + field);
            if (!f) continue;
            const int64_t cur = app.db.get_int(*p, rec, field, 0);
            const OpVp v = op_vp(*p, rec, field, false);
            CHECK(v.open, std::string("the picker of ") + field + " opens (catalogue active)");
            CHECK(v.unique(), std::string("ONE merged list, no code twice (catalogue active): ") + field);
            std::set<int64_t> need = op_rdbm(field);
            for (int64_t c : gallery_codes(app, field)) need.insert(c);
            for (int64_t c : code_value_list(app, *p, *f, cur)) need.insert(c);
            need = op_strip(need, cur);
            std::set<int64_t> pass;  // what the catalogue gate lets through (the same call the tile uses)
            for (int64_t c : need)
                if (gallery_write_ok(app, field, false, c, nullptr)) pass.insert(c);
                else ++gated;
            const std::string listed = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (catalogue active) ") + field + " listed", need, v.all());
            CHECK(listed.empty(), listed);
            const std::string wr = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (catalogue active) ") + field + " writable", pass, v.writable);
            CHECK(wr.empty(), wr);
            CHECK(!pass.empty(), std::string("fixture: the gate lets some code of ") + field + " through");
            ++compared;
        }
        CHECK(compared == 5, fmt("fields compared (%d)", compared));
        std::printf("  ONE-PICKER catalogue active: %d fields; %d codes refused by the gate (listed, greyed)\n", compared, gated);
        // the accessory pairs the gate lets through (players' set: 6..17, 22..27) are writable although the fixture catalogue lists only a preview of 40
        const OpVp acc = op_vp(*p, rec, "accessorycode1", false);
        for (int64_t c : {6, 8, 10, 12, 14, 22, 24, 26})
            if (c != app.db.get_int(*p, rec, "accessorycode1", 0)) CHECK(acc.writable.count(c) > 0, "accessory " + std::to_string(c) + " (a player accessory) is a writable tile with the catalogue active");
        set_gear_catalog(nullptr);
        op_fixture_off(before);
    });

    // ---- fix round 2 (reviewer D1, 717aa116 moved Sleeves and Sock length from enum_editor to the picker's inline rows): a field with a
    // picture set and labelled codes but NO gallery must offer every labelled code too (the merge was gated on the gallery alone)
    run_case("UI 3.2 one picker (fix 2): non-gallery fields offer every labelled code: Sleeves 0..4 and Sock length 0..3 first, then every labelled non-gallery look field", [&] {
        const Table* p = app.db.table("players");
        const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
        CHECK(p && rec != 0, "the test world has player 2002");
        if (!p || !rec) return;
        set_gear_catalog(nullptr);
        const std::string before = op_fixture_on();
        op_draw(p, rec);
        // the literal codes of field_labels.h (jerseysleevelengthcode 0..4, socklengthcode 0..3 "Low"): written out here, not read from the
        // table the fix reads, so the test cannot agree with the code by construction
        struct Lit {
            const char* field;
            std::vector<int64_t> codes;
        };
        for (const Lit& l : std::vector<Lit>{{"jerseysleevelengthcode", {0, 1, 2, 3, 4}}, {"socklengthcode", {0, 1, 2, 3}}}) {
            const Field* f = p->field(l.field);
            CHECK(f != nullptr, std::string("fixture: the players table has ") + l.field);
            if (!f) continue;
            const OpVp v = op_vp(*p, rec, l.field, false);
            CHECK(v.open, std::string("the picker of ") + l.field + " opens");
            CHECK(v.unique(), std::string("ONE merged list, no code twice: ") + l.field);
            CHECK(!gallery_has(l.field), std::string("fixture: ") + l.field + " has no gallery (the case this fix is about)");
            const std::set<int64_t> need(l.codes.begin(), l.codes.end());
            const std::string miss = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (non-gallery) ") + l.field + " vs field_labels.h", need, v.all());
            CHECK(miss.empty(), miss);
            const std::string wr = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (non-gallery) ") + l.field + " writable", need, v.writable);
            CHECK(wr.empty(), wr);
        }
        // EVERY field the picker can open (the compiled field table, mapped_fields), players and managers: the offered set of a field with no gallery
        // must hold every labelled code (enum_defs or named codes, inside the field's range) and the old combo's list. A field with no labelled
        // code (eyebrows, tattoos, head type, run-up beyond the three standard ones) is unchanged by the fix and is only listed. The list printed
        // per field (picture set size, labelled codes, offered codes, the ones outside the picture set) is the report's "offered set changes" table.
        int compared = 0, unchanged = 0, absent = 0;
        std::set<std::string> exact_checked;  // P3 (fresh review): the unlabelled fields whose offered list is asserted exactly
        const Table* mt = app.db.table("manager");
        const auto& mgrs = app.model.managers();
        const uint64_t mrec = (mt && !mgrs.empty()) ? mgrs[0].rec : 0;
        for (const bool manager : {false, true}) {
            const Table* tt = manager ? mt : p;
            const uint64_t trec = manager ? mrec : rec;
            if (!tt || !trec) continue;
            for (const std::string& field : turbo::visual::mapped_fields()) {
                const Field* f = tt->field(field);
                if (!f) {
                    ++absent;
                    continue;
                }
                if (gallery_has(field, manager)) continue;  // a gallery field already merged (fix 1): same branch, not changed by fix 2
                std::set<int64_t> labelled;
                if (const labels::EnumDef* e = labels::enum_def(field))
                    for (size_t k = 0; k < e->labels.size(); ++k) labelled.insert(e->base + static_cast<int64_t>(k));
                else if (const labels::NamedCodes* n = labels::named_codes(field))
                    for (const auto& e : *n) labelled.insert(e.first);
                for (auto it = labelled.begin(); it != labelled.end();) it = (*it < f->min || *it > f->max()) ? labelled.erase(it) : std::next(it);
                const int64_t cur = app.db.get_int(*tt, trec, field, 0);
                std::set<int64_t> need = labelled;
                if (!labelled.empty())
                    for (int64_t c : code_value_list(app, *tt, *f, cur)) need.insert(c);  // the old combo's list (only where the field has labelled codes)
                const OpVp v = op_vp(*tt, trec, field, manager);
                CHECK(v.open, std::string("the picker of ") + field + " opens");
                CHECK(v.unique(), std::string("ONE merged list, no code twice: ") + field);
                const std::string who = manager ? "(manager) " : "";
                const std::string miss = op_missing(std::string("ONE PICKER NO LOST CAPABILITY (non-gallery) ") + who + field + " vs field_labels.h and the old combo's list", op_strip(need, cur), v.all());
                CHECK(miss.empty(), miss);
                // P3 (fresh review, mutant c1): a field with NO labelled code (the product's own condition: no enum_def, no named codes, no gallery) is
                // UNCHANGED by the merge: the offered list is EXACTLY the picture set plus his value, as a set, not a count (build_source comment
                // "A field with no labelled code ... is unchanged"). Mutant c1 (merge condition forced true) goes red on headtypecode ONLY (offered 622 codes,
                // picture set + his value 611); tattoohead is unchanged by c1 (228 both). A code swap inside tattoohead's picture set goes red with EQUAL counts,
                // because the check compares the sets, not their sizes. An EMPTY picture set must not pass vacuously (his value against itself): the CHECK below.
                if (!labels::enum_def(field) && !labels::named_codes(field)) {
                    std::set<int64_t> exact = op_rdbm(field);
                    CHECK(!exact.empty(), std::string("ONE PICKER UNLABELLED FIELD has a non-empty picture set (else the comparison is his value against itself) ") + who + field);
                    exact.insert(cur);
                    const std::set<int64_t> got = v.all();
                    CHECK(got == exact, std::string("ONE PICKER UNLABELLED FIELD UNCHANGED ") + who + field + ": offered " + std::to_string(got.size()) + " codes, picture set + his value " +
                                            std::to_string(exact.size()) + ": " + op_diff(exact, got));
                    exact_checked.insert((manager ? "mgr:" : "") + field);
                }
                // the offered set that changed against the picture set alone: printed per field for the report
                const std::set<int64_t> pic = op_rdbm(field);
                std::string added;
                int nadd = 0;
                for (int64_t c : v.all())
                    if (!pic.count(c) && c != cur && ++nadd <= 8) added += (added.empty() ? "" : ",") + std::to_string(c);
                std::printf("  ONE-PICKER fix2 %s%-26s picture set %3zu codes, labelled %3zu, offered %3zu, not in the picture set: %d%s%s\n", manager ? "[mgr] " : "", field.c_str(),
                            pic.size(), labelled.size(), v.codes.size(), nadd, nadd ? " e.g. " : "", added.c_str());
                (labelled.empty() ? unchanged : compared)++;
            }
        }
        std::printf("  ONE-PICKER fix2 summary: %d non-gallery fields with labelled codes compared, %d without (unchanged), %d mapped fields absent from the test tables\n", compared, unchanged, absent);
        CHECK(compared >= 6, fmt("non-gallery fields with labelled codes compared (%d)", compared));
        CHECK(exact_checked.count("headtypecode") && exact_checked.count("tattoohead"), "P3: headtypecode and tattoohead (players) are among the unlabelled fields asserted exactly");
        std::printf("  ONE-PICKER P3 unlabelled fields asserted exactly (offered == picture set + his value): %zu\n", exact_checked.size());
        op_fixture_off(before);
    });

    // ---- the Simple player page, no RDBM26 folder: "Choose a real face..." keeps the Eyes and Skin boxes (q6-nopicker)
    run_case("UI 3.2 one picker (fix 1): Simple player page, no RDBM26 folder: the real-face chooser keeps its Eyes and Skin boxes, never Hair / Beard; with a folder they go", [&] {
        const int64_t saved_sel = app.sel_player;
        app.sel_player = 1001;
        visual_pictures_override((test_tmp_dir("one_picker_simple") / "nowhere").u8string());
        visual_pictures_refresh(app);
        player_page_test::show(ui);
        CHECK(ui.click("###pp_face", "pp_looks"), "the Simple page's Choose a real face button");
        ui.frames(3);
        if (const ItemRec* own = ui.find("His own head##faces")) {
            const std::string win = own->window;
            CHECK(ui.find("Hair", win) == nullptr && ui.find("Beard", win) == nullptr, "no Hair / Beard copy boxes (their pickers are in Appearance)");
            CHECK(ui.find("Eyes", win) != nullptr && ui.find("Skin", win) != nullptr,
                  "NO LOST CAPABILITY: with no RDBM26 folder the Simple page's chooser keeps its Eyes and Skin boxes (no picker on the card gives them)");
            const fs::path pics = test_tmp_dir("one_picker_simple_rdbm") / "Resources";
            for (const char* sub : {"runstyle/runstyle_0.gif", "eyecolor/eyecolor_0.png", "skintone/skintone_1.png"}) {
                fs::create_directories((pics / sub).parent_path());
                std::ofstream(pics / sub, std::ios::binary) << "\x89PNG";
            }
            visual_pictures_override(pics.u8string());
            visual_pictures_refresh(app);
            ui.frames(3);
            CHECK(ui.find("Eyes", win) == nullptr && ui.find("Skin", win) == nullptr, "with an RDBM26 folder the card has the eye colour / skin tone pickers and the boxes go");
        } else {
            CHECK(false, "the chooser opened from the Simple page (His own head)");
        }
        ui.click("Close##faces");
        ui.frames(2);
        player_page_test::hide(ui);
        visual_pictures_override("");
        visual_pictures_refresh(app);
        app.sel_player = saved_sel;
    });
}
