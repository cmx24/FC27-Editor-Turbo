// Native tests (4.0 GUI overhaul, slice S6): Players > Edit a player > Positions, attributes & playstyles (ui/ui_player_profile.cpp,
// draw_player_ability and the helpers only it calls).
//
// HOW THIS FILE IS INCLUDED: test_main.cpp holds ONE line for it, `#include "test_ability_40.h"`, INSIDE the UI block (right after the
// profile_page_test::ui_cases call), so the statements below run there with `app`, `ui` and `le` in scope. It is therefore a sequence of
// statements, not a header: no #pragma once, no #include, no namespace and no function definitions (they would land inside a function).
//
// What is guarded:
//   - the tab draws the Player type & tier card (the archetype panel) FIRST, then Overall, Positions, Style and roles, Attributes, PlayStyles;
//   - a coded field shows its descriptor in words: the Positions line (position_text: "ST", "RCB (internal, plays as CB)"), the Roles line
//     (the game's role names), and one line per PlayStyle / Trait set naming the ticked boxes (a box past the name list reads "Unnamed trait N": not reachable in the
//     fixture, whose trait fields are as wide as the name lists; the Roles line is checked only when the table has role fields, which the
//     fixture world does not: both are named as untested here in the S6 report);
//   - no help text of the tab shows a field name (trait1 ...: rule R4 / R9) and the stand-in text names no integration step;
//   - the two grids of the tab are tables in the registry form (table_registry.h ids "##bits" and "##sl", tbl::kGrid flags).
// Plain text is not an ImGui item, so the tab reports what it said in words through turbo_ability_words_40() (global function in
// ui_player_profile.cpp). It is declared WEAK here on purpose: on the pre-S6 code (ce664c6b) the symbol does not exist, the case then
// FAILS with a CHECK instead of the whole test binary failing to link, which is what the fail-old run shows.
{
    const std::vector<std::string>& turbo_ability_words_40() __attribute__((weak));
    const char* W = "##profilepage_test";
    const auto words = [&]() -> std::vector<std::string> {
        return &turbo_ability_words_40 != nullptr ? turbo_ability_words_40() : std::vector<std::string>{};
    };
    // the text of the last line the tab said that starts with `key` ("" when there is none)
    const auto said = [&](const std::string& key) -> std::string {
        for (const std::string& s : words())
            if (s.compare(0, key.size(), key) == 0) return s.substr(key.size());
        return "";
    };
    const auto index_of = [&](const std::string& key) -> int {
        const std::vector<std::string> v = words();
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == key) return static_cast<int>(i);
        return -1;
    };

    run_case("S6 ability tab: the tab reports the words it drew, and draws the Player type & tier card before Positions", [&] {
        app.sel_player = 1001;
        app.prefs.advanced_mode = false;
        profile_page_test::show(ui, profile_page_test::Which::Ability);
        CHECK(&turbo_ability_words_40 != nullptr, "the tab reports its words (turbo_ability_words_40)");
        // the editor window keeps its scroll between cases, so the card can be above the view: reached by scrolling, as a user does. The screen
        // geometry (card above Overall above Positions) is checked in test_ia_222.h; here the order is the one the tab drew.
        CHECK(profile_page_test::reach(ui, "###ab_typetier", W) != nullptr, "the Player type & tier card is on the page");
        CHECK(profile_page_test::reach(ui, "###ab_positions", W) != nullptr, "Positions is on the page");
        const int a = index_of("section ab_typetier"), o = index_of("section ab_ovr"), p = index_of("section ab_positions"), s = index_of("section ab_style"),
                  at = index_of("section ab_attrs"), ps = index_of("section ab_styles");
        CHECK(a == 0, "the Player type & tier card is the first section drawn: " + std::to_string(a));
        CHECK(a >= 0 && a < o && o < p && p < s && s < at && at < ps, "order drawn: card, Overall, Positions, Style and roles, Attributes, PlayStyles");
        CHECK(player_ability_state().archetype_bar_external && player_ability_state().bar_calls == 1, "the player type editor is the card's, drawn once");
        profile_page_test::hide(ui);
    });

    run_case("S6 ability tab: coded fields show their descriptors (positions, roles, PlayStyle and Trait boxes)", [&] {
        app.sel_player = 1004;
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1004);
        CHECK(t && pr, "player 1004");
        if (!t || !pr) return;
        const size_t undo0 = app.undo_count(1004);
        const auto put = [&](const char* field, int64_t v) {
            const Field* f = t->field(field);
            if (f) app.edit(*t, pr->rec, *f, Value::of_int(v));
            return f != nullptr;
        };
        // a known state: main position ST, second position RCB (a code no game menu names), roles 42 and 152 (only when the table has the role
        // fields: the GUI fixture world, gui_world.lua, has none), two PlayStyles, one PlayStyle+, Traits bit 13 (Super Sub). A box past the name
        // list ("Unnamed trait N") cannot be built here: the fixture's trait fields are as wide as the name lists.
        const bool has_roles = t->field("role1") != nullptr && t->field("role2") != nullptr;
        const bool set = put("preferredposition1", 25) && put("preferredposition2", 4) && (!has_roles || (put("role1", 42) && put("role2", 152))) &&
                         put("trait1", (int64_t(1) << 0) | (int64_t(1) << 21)) && put("icontrait1", int64_t(1) << 2) && put("trait2", int64_t(1) << 13);
        CHECK(set, "the fixture player's coded fields were set");
        profile_page_test::show(ui, profile_page_test::Which::Ability);
        CHECK(&turbo_ability_words_40 != nullptr, "the tab reports its words");
        const std::string pos = said("words positions: ");
        CHECK(pos.find("ST") != std::string::npos && pos.find("RCB (internal, plays as CB)") != std::string::npos, "positions in words: '" + pos + "'");
        const std::string roles = said("words roles: ");
        if (has_roles)
            CHECK(roles.find(labels::describe("players", "role1", 42)) != std::string::npos && roles.find("ST Poacher+") != std::string::npos &&
                      roles.find("LW False Winger++") != std::string::npos,
                  "roles in words: '" + roles + "'");
        else
            CHECK(roles.empty(), "no role fields in this table: no roles line");
        CHECK(said("words PlayStyles: ") == "Finesse Shot, Rapid", "PlayStyles in words: '" + said("words PlayStyles: ") + "'");
        CHECK(said("words PlayStyles+: ") == "Power Shot", "PlayStyles+ in words: '" + said("words PlayStyles+: ") + "'");
        CHECK(said("words Traits: ") == "Super Sub", "Traits in words: '" + said("words Traits: ") + "'");
        CHECK(said("words Traits+: ") == "none ticked", "Traits+ with nothing ticked: '" + said("words Traits+: ") + "'");
        // help lines: no field name
        for (const char* set_title : {"PlayStyles", "PlayStyles+", "Traits", "Traits+"}) {
            const std::string h = said(std::string("help ") + set_title + ": ");
            CHECK(!h.empty() && h.find("trait") == std::string::npos && h.find("Stored as") == std::string::npos, std::string("help of ") + set_title + " names no field: '" + h + "'");
        }
        CHECK(said("help stand-in: ").find("wired") == std::string::npos, "the stand-in text names no integration step");
        // the descriptor follows an edit by the next frame (a ticked box, in words)
        const Field* f1 = t->field("trait1");
        if (f1) {
            app.edit(*t, pr->rec, *f1, Value::of_int(int64_t(1) << 4));
            ui.frames(2);
            CHECK(said("words PlayStyles: ") == "Precision Header", "after an edit: '" + said("words PlayStyles: ") + "'");
        }
        profile_page_test::hide(ui);
        while (app.undo_count(1004) > undo0) app.undo(1004);  // the fixture player as it was
    });

    run_case("S6 ability tab: its grids are tables in the registry form (##sl, ##bits) and its helpers have no raw BeginTable", [&] {
        bool sl = false, bits = false;
        for (const tbl::Spec& s : tbl::all()) {
            if (std::string(s.id) == "##sl") sl = (s.flags & tbl::kGrid) == tbl::kGrid;
            if (std::string(s.id) == "##bits") bits = (s.flags & tbl::kGrid) == tbl::kGrid;
        }
        CHECK(sl && bits, "##sl and ##bits are registered with the kGrid flags");
        const fs::path src = fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui" / "ui_player_profile.cpp";
        const std::string text = read_file(src);
        CHECK(!text.empty(), "ui_player_profile.cpp at " + src.string());
        // every BeginTable( of the file names tbl:: in its flags (the registry form), the field_grid tables included (widgets.cpp)
        int calls = 0, raw = 0;
        for (size_t at = text.find("BeginTable("); at != std::string::npos; at = text.find("BeginTable(", at + 1)) {
            ++calls;
            const size_t end = text.find(';', at);
            const std::string call = text.substr(at, end == std::string::npos ? 160 : end - at);
            if (call.find("tbl::") == std::string::npos) ++raw;
        }
        CHECK(calls >= 2 && raw == 0, "BeginTable calls in ui_player_profile.cpp: " + std::to_string(calls) + ", raw: " + std::to_string(raw));
    });
}
