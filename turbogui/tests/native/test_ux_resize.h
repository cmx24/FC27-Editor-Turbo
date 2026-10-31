// Native tests (2026-10-11, Cassio: "resizable everything", "always loading images at startup"): the table registry and
// the sources it describes (ui/table_registry.h), the resizable child panes, the picture preload order and its
// cancellation (ui/preload_queue.h), and the display preferences with their defaults (ui/gui_prefs.h).
// Included by test_main.cpp after the Ui driver; test_ux_resize() runs with the pure cases.
#pragma once
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "ui/gui_prefs.h"
#include "ui/preload_queue.h"
#include "ui/table_registry.h"
#include "ui/ui_split.h"

namespace {

// The turbogui folder: g_lua is <turbogui>/tests/native/gui_world.lua
fs::path ux_src_ui() { return fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui"; }

// Every "ImGui::BeginXxx(" call in `text`: the argument list as written (parentheses balanced, string literals skipped)
std::vector<std::string> ux_calls(const std::string& text, const std::string& fn) {
    std::vector<std::string> out;
    const std::string needle = fn + "(";
    size_t at = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        size_t i = at + needle.size();
        int depth = 1;
        bool in_str = false;
        std::string args;
        for (; i < text.size() && depth > 0; ++i) {
            const char c = text[i];
            if (in_str) {
                if (c == '\\') { args += c; args += text[++i]; continue; }
                if (c == '"') in_str = false;
            } else if (c == '"') in_str = true;
            else if (c == '(') ++depth;
            else if (c == ')') { if (--depth == 0) break; }
            args += c;
        }
        out.push_back(args);
        at = i;
    }
    return out;
}

// The string literals of the first argument ("##players"; two for `a ? "##x" : "##y"`)
std::vector<std::string> ux_first_arg_literals(const std::string& args) {
    std::vector<std::string> ids;
    int depth = 0;
    bool in_str = false;
    std::string cur;
    for (size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (in_str) {
            if (c == '"') { ids.push_back(cur); cur.clear(); in_str = false; }
            else cur += c;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) break;
    }
    return ids;
}

}  // namespace

static void test_ux_resize() {
    run_case("tables: the registry (unique stable IDs, resizable + reorderable + hideable, one sizing policy)", [&] {
        const auto& all = tbl::all();
        CHECK(all.size() >= 27, fmt("%zu tables registered", all.size()));
        std::set<std::string> ids;
        for (const tbl::Spec& s : all) {
            CHECK(ids.insert(s.id).second, std::string("ID once: ") + s.id);
            CHECK(std::string(s.id).rfind("##", 0) == 0, std::string("ID is a ## literal (stable in the layout file): ") + s.id);
            if (s.popup) CHECK((s.flags & tbl::kColumns) == 0, std::string("a table inside a popup keeps fixed columns: ") + s.id);
            else CHECK((s.flags & tbl::kColumns) == tbl::kColumns, std::string("resizable, reorderable, hideable: ") + s.id);
            CHECK((s.flags & ImGuiTableFlags_NoSavedSettings) == 0, std::string("widths / order saved in the layout file: ") + s.id);
            const ImGuiTableFlags sizing = tbl::sizing_of(s.flags);
            CHECK(sizing == 0 || sizing == ImGuiTableFlags_SizingFixedFit || sizing == ImGuiTableFlags_SizingFixedSame ||
                      sizing == ImGuiTableFlags_SizingStretchProp || sizing == ImGuiTableFlags_SizingStretchSame,
                  std::string("one sizing policy: ") + s.id);
            CHECK(s.scrolls == ((s.flags & ImGuiTableFlags_ScrollY) != 0), std::string("scrolls <=> ScrollY: ") + s.id);
            CHECK(s.where && *s.where, std::string("has a screen: ") + s.id);
        }
        CHECK(tbl::find("##players") && tbl::find("##players")->scrolls && !tbl::find("##nope"), "find");
    });

    run_case("tables: every BeginTable in src/ui carries the column flags and its ID is registered; every registered ID exists", [&] {
        const fs::path dir = ux_src_ui();
        CHECK(fs::is_directory(dir), "src/ui at " + dir.string());
        std::string everything;
        size_t calls = 0;
        std::set<std::string> literal_ids;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.path().extension() != ".cpp") continue;
            const std::string text = read_file(e.path());
            everything += text;
            for (const std::string& args : ux_calls(text, "ImGui::BeginTable")) {
                ++calls;
                const std::string where = e.path().filename().string() + ": BeginTable(" + args.substr(0, std::min<size_t>(args.size(), 60)) + "...";
                const bool via_registry = args.find("tbl::") != std::string::npos;
                const bool literal = args.find("ImGuiTableFlags_Resizable") != std::string::npos &&
                                     args.find("ImGuiTableFlags_Reorderable") != std::string::npos &&
                                     args.find("ImGuiTableFlags_Hideable") != std::string::npos;
                // flags held in a variable (fl, tf): the variable's definition in the same file must carry them
                bool variable = false;
                if (!via_registry && !literal) {
                    std::string flags_arg;
                    int depth = 0, commas = 0;
                    for (char c : args) {
                        if (c == '(') ++depth;
                        else if (c == ')') --depth;
                        else if (c == ',' && depth == 0) { ++commas; continue; }
                        if (commas == 2) flags_arg += c;
                    }
                    flags_arg.erase(0, flags_arg.find_first_not_of(" \t\r\n"));
                    flags_arg.erase(flags_arg.find_last_not_of(" \t\r\n") + 1);
                    if (!flags_arg.empty()) {
                        const size_t def = text.find("ImGuiTableFlags " + flags_arg + " =");
                        if (def != std::string::npos) {
                            const std::string body = text.substr(def, text.find(';', def) - def);
                            variable = body.find("ImGuiTableFlags_Resizable") != std::string::npos &&
                                       body.find("ImGuiTableFlags_Reorderable") != std::string::npos &&
                                       body.find("ImGuiTableFlags_Hideable") != std::string::npos;
                        }
                    }
                }
                // a table inside an auto-sizing popup keeps fixed columns (table_registry.h Spec::popup)
                bool popup = false;
                for (const std::string& id : ux_first_arg_literals(args)) {
                    literal_ids.insert(id);
                    const tbl::Spec* s = tbl::find(id.c_str());
                    CHECK(s != nullptr, "registered: " + id + " (" + e.path().filename().string() + ")");
                    popup = popup || (s && s->popup);
                }
                CHECK(via_registry || literal || variable || popup, "resizable + reorderable + hideable: " + where);
            }
        }
        CHECK(calls >= 26, fmt("%zu BeginTable calls scanned", calls));
        for (const tbl::Spec& s : tbl::all())
            CHECK(everything.find(std::string("\"") + s.id + "\"") != std::string::npos, std::string("exists in src/ui: ") + s.id);
        CHECK(literal_ids.size() >= 25, fmt("%zu literal table IDs", literal_ids.size()));
        // the Database tab keeps one table per database table: its ID is pushed before BeginTable
        const std::string db = read_file(dir / "ui_database.cpp");
        CHECK(db.find("ImGui::PushID(g_view.table.c_str())") != std::string::npos && db.find("ImGui::PushID(g_view.table.c_str())") < db.find("BeginTable(\"##dbt\""),
              "Database: table name on the ID stack before ##dbt");
    });

    run_case("panes (2.22): EVERY BeginChild in src/ui that sits beside another pane has a resize flag or is on the commented allow-list", [&] {
        // No hard-coded list of panes: each BeginChild is read where it stands. "Beside another pane" = it has a fixed width in its size argument,
        // or the next SameLine follows its EndChild directly (a left pane). It then needs ResizeX / AutoResizeX in its flags, or must be a pane that
        // is already resized by its neighbour, a popup / modal list, or owned by another track until merge (ui_split.h: they call pane_x after merge).
        struct Allow { const char* id; const char* why; };
        const Allow allow[] = {
            {"##selectrail", "the 36 px rail the select panel collapses to under 900 px: not a pane to resize (the wide pane is split::pane_x '##select')"},
            {"##bk_fieldlist", "Bulk edit's field list inside a modal picker (fixed size next to nothing)"},
            {"##fvals", "a value list inside a modal popup (face chooser counts)"},
            {"##vpgrid", "G-pickers: the visual picker's tile grid; calls split::pane_x after the merge (brief 2.22 field 1a)"},
            {"##vppreview", "G-pickers: the visual picker's preview, the remainder next to ##vpgrid"},
            {"##lscards", "G-ia: the Appearance Editor's cards; ##lslist / ##lspreview beside it already have ResizeX; calls split::pane_x after the merge"},
            {"##tacright", "the remainder of the Tactics row: ##tacleft and ##taccentre beside it have ResizeX, so dragging their borders resizes this one"},
            {"##complist", "the competition picker's list inside a popup"},
            {"##rail", "the 3.0 shell's area rail (app.cpp ##rail): a fixed 156 / 44 px shell rail with its own narrow toggle, not a pane; no resize flag "
                       "(brief 2.22-G-resize.md:66,76; Pit wall 19:36 2026-10-26, 4.0c)"},
        };
        auto allowed = [&](const std::string& id) {
            for (const Allow& a : allow) if (id == a.id) return true;
            return false;
        };
        const fs::path dir = ux_src_ui();
        size_t scanned = 0, beside = 0;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.path().extension() != ".cpp") continue;
            const std::string text = read_file(e.path());
            size_t at = 0;
            while ((at = text.find("ImGui::BeginChild(", at)) != std::string::npos) {
                const size_t call = at;
                at += 18;
                ++scanned;
                const std::vector<std::string> one = ux_calls(text.substr(call), "ImGui::BeginChild");
                if (one.empty()) continue;
                const std::string args = one[0];
                const std::vector<std::string> ids = ux_first_arg_literals(args);
                const std::string id = ids.empty() ? std::string("<computed>") : ids[0];
                // the size argument: the text after the first top-level comma up to the next top-level comma
                std::string size_arg;
                {
                    int depth = 0, commas = 0;
                    bool in_str = false;
                    for (char c : args) {
                        if (in_str) { if (c == '"') in_str = false; continue; }
                        if (c == '"') in_str = true;
                        else if (c == '(') ++depth;
                        else if (c == ')') --depth;
                        else if (c == ',' && depth == 0) { ++commas; continue; }
                        if (commas == 1) size_arg += c;
                    }
                }
                bool fixed_w = false;
                {
                    const size_t v = size_arg.find("ImVec2(");
                    if (v != std::string::npos) {
                        std::string x;
                        int depth = 0;
                        for (size_t i = v + 7; i < size_arg.size(); ++i) {
                            const char c = size_arg[i];
                            if (c == '(') ++depth;
                            else if (c == ')') { if (depth == 0) break; --depth; }
                            else if (c == ',' && depth == 0) break;
                            x += c;
                        }
                        x.erase(0, x.find_first_not_of(" \t"));
                        x.erase(x.find_last_not_of(" \t") + 1);
                        fixed_w = !(x == "0" || x == "0.0f" || x == "-FLT_MIN" || x.empty());
                    }
                }
                // the matching EndChild, then: does a SameLine follow?
                bool same_line_after = false;
                {
                    int depth = 1;
                    size_t i = call + 18;
                    while (depth > 0) {
                        const size_t nb = text.find("ImGui::BeginChild(", i), ne = text.find("ImGui::EndChild(", i);
                        if (ne == std::string::npos) break;
                        if (nb != std::string::npos && nb < ne) { ++depth; i = nb + 18; }
                        else { --depth; i = ne + 16; }
                    }
                    if (depth == 0) {
                        const size_t semi = text.find(';', i);
                        const std::string tail = text.substr(semi == std::string::npos ? i : semi + 1, 40);
                        same_line_after = tail.find("ImGui::SameLine(") != std::string::npos && tail.find("ImGui::SameLine(") < 24;
                    }
                }
                if (!fixed_w && !same_line_after) continue;
                ++beside;
                const bool flagged = args.find("ResizeX") != std::string::npos || args.find("ImGuiChildFlags_Resize") != std::string::npos;
                const bool auto_h = args.find("AutoResizeY") != std::string::npos && !fixed_w;  // a card that only grows downward
                CHECK(flagged || allowed(id) || auto_h, std::string(e.path().filename().string()) + " " + id + " sits beside another pane with no ResizeX and is not on the allow-list: " + args.substr(0, 90));
            }
        }
        CHECK(scanned >= 80, fmt("%zu BeginChild calls scanned", scanned));
        CHECK(beside >= 8, fmt("%zu panes beside another were found (the scan sees the split rows)", beside));
        for (const Allow& a : allow) CHECK(a.why && *a.why, std::string("allow-list entry explained: ") + a.id);
        // the select panel of every page::frame page goes through the shared helper, not a plain child
        const std::string shell_src = read_file(dir / "ui_shell.cpp");
        CHECK(shell_src.find("split::pane_x(\"##select\"") != std::string::npos, "page::frame draws ##select with split::pane_x");
        CHECK(shell_src.find("BeginChild(\"##select\"") == std::string::npos, "no plain BeginChild(\"##select\") is left");
        const std::string split_src = read_file(dir / "ui_split.h");
        CHECK(split_src.find("ImGuiChildFlags_ResizeX") != std::string::npos && split_src.find("ImGuiChildFlags_ResizeY") != std::string::npos &&
                  split_src.find("pane_x") != std::string::npos && split_src.find("pane_y") != std::string::npos,
              "ui_split.h has pane_x (ResizeX) and pane_y (ResizeY)");
    });

    run_case("main panel (2.22): the flags never contain NoResize or NoMove", [&] {
        const std::string app_src = read_file(ux_src_ui() / "app.cpp");
        bool found = false;
        for (const std::string& args : ux_calls(app_src, "ImGui::Begin")) {
            if (args.find("\"FC 27 LE Turbo\"") == std::string::npos) continue;
            found = true;
            CHECK(args.find("NoResize") == std::string::npos && args.find("NoMove") == std::string::npos, "Begin(\"FC 27 LE Turbo\") flags: " + args);
        }
        CHECK(found, "the main panel's Begin is in app.cpp");
        CHECK(app_src.find("ConfigWindowsMoveFromTitleBarOnly = wprefs.maximized") != std::string::npos,
              "while maximized only the title bar moves it (a stray drag on the page does not un-maximize it)");
    });

    run_case("tables (2.22): every BeginTable outside a popup has a header row, or is a layout grid, or belongs to another track until merge", [&] {
        // A layout grid (tbl::kGrid: label + editor cells) has nothing to head. The header-less data tables below belong to other tracks
        // (G-ia ui_player_profile.cpp, G-pickers ui_visual_picker.cpp) or sit in a popup the registry cannot tell (##comptable); they are named
        // one by one, so a NEW header-less table in the same files still fails. Remove each line when its owner has added the header row.
        const std::set<std::string> other_tracks = {"ui_player_profile.cpp|##players", "ui_visual_picker.cpp|##vrows", "ui_visual_picker.cpp|##vrow",
                                                    "ui_competitions.cpp|##comptable"};
        size_t tables = 0, headed = 0, grids = 0, parked = 0, popups = 0;
        for (const auto& e : fs::directory_iterator(ux_src_ui())) {
            if (e.path().extension() != ".cpp") continue;
            const std::string text = read_file(e.path());
            size_t at = 0;
            while ((at = text.find("ImGui::BeginTable(", at)) != std::string::npos) {
                const size_t call = at;
                at += 18;
                const std::vector<std::string> one = ux_calls(text.substr(call), "ImGui::BeginTable");
                if (one.empty()) continue;
                ++tables;
                const std::string args = one[0];
                const std::vector<std::string> ids = ux_first_arg_literals(args);
                const std::string id = ids.empty() ? std::string("<computed>") : ids[0];
                const size_t end = text.find("ImGui::EndTable(", call);
                const std::string body = text.substr(call, end == std::string::npos ? 4000 : end - call);
                const bool header = body.find("TableHeadersRow") != std::string::npos || body.find("TableHeader(") != std::string::npos;
                const tbl::Spec* sp = ids.empty() ? nullptr : tbl::find(id.c_str());
                const bool popup = sp && sp->popup;
                const bool grid = args.find("tbl::kGrid") != std::string::npos;
                if (header) { ++headed; continue; }
                if (grid) { ++grids; continue; }
                if (popup) { ++popups; continue; }
                if (other_tracks.count(e.path().filename().string() + "|" + id)) { ++parked; continue; }
                CHECK(false, std::string(e.path().filename().string()) + " " + id + ": a data table without a header row (add TableHeadersRow, or name it above with its owner)");
            }
        }
        CHECK(tables >= 45, fmt("%zu BeginTable calls scanned", tables));
        CHECK(headed >= 25, fmt("%zu tables have a header row", headed));
        std::printf("      header rows: %zu headed, %zu layout grids, %zu popups, %zu parked for G-ia / G-pickers\n", headed, grids, popups, parked);
    });

    run_case("splits (2.22): the clamp (180 px .. 70 %), the store round trip, the damaged-file defaults", [&] {
        CHECK(clamp_split_width(300, 180, 1920, 0.7f) == 300 && clamp_split_width(50, 180, 1920, 0.7f) == 180 && clamp_split_width(5000, 180, 1920, 0.7f) == 1344,
              "300 stays; 50 -> 180; 5000 -> 70 % of 1920");
        CHECK(clamp_split_width(500, 180, 200, 0.7f) == 180, "a panel so small that 70 % is under the minimum: the minimum wins");
        CHECK(clamp_split_width(NAN, 180, 1920, 0.7f) == 180, "NaN -> minimum");
        SplitStore st;
        CHECK(!st.has("PlayerEdit/##select") && st.get("PlayerEdit/##select", 300) == 300 && !st.dirty, "nothing stored: the default, not dirty");
        st.set("PlayerEdit/##select", 512.4f);
        st.set("Kits/##select", 241.0f);
        CHECK(st.dirty && st.get("PlayerEdit/##select", 0) == 512 && st.take_dirty() && !st.dirty && !st.take_dirty(), "stored rounded; taken once");
        st.set("PlayerEdit/##select", 512.0f);
        CHECK(!st.dirty, "the same value again is not a change");
        st.set("x", NAN);
        st.set("y", -4.0f);
        CHECK(!st.has("x") && !st.has("y"), "NaN and negative widths are refused");
        json j = json::parse("{\"gui\":{\"ui_scale\":1.2}}");
        st.save(j["gui"]);
        CHECK(j["gui"]["splits"]["PlayerEdit/##select"] == 512 && j["gui"]["splits"]["Kits/##select"] == 241 && j["gui"]["ui_scale"] == 1.2, "written, other keys kept");
        SplitStore back;
        back.load(j["gui"]);
        CHECK(back.get("PlayerEdit/##select", 0) == 512 && back.get("Kits/##select", 0) == 241 && !back.dirty, "round trip");
        SplitStore bad;
        bad.load(json::parse("{\"splits\":{\"a\":\"wide\",\"b\":null,\"c\":-3,\"d\":250}}"));
        CHECK(bad.widths.size() == 1 && bad.get("d", 0) == 250, "junk entries are dropped");
        bad.load(json::parse("{\"splits\":7}"));
        CHECK(bad.widths.empty(), "a damaged block is no widths");
        GuiPrefs gp;
        gp.splits.set("Standings/##select", 333);
        json g = json::object();
        gp.save(g);
        CHECK(GuiPrefs::load(g).splits.get("Standings/##select", 0) == 333 && !GuiPrefs::load(g).splits.dirty, "GuiPrefs save / load carries gui.splits");
    });

    run_case("row height (2.22): range, defaults, old row_density files, round trip, the style numbers", [&] {
        CHECK(GuiPrefs::load(json::object()).row_height == kRowHeightNormal, "default = normal");
        CHECK(clamp_row_height(5) == kRowHeightMin && clamp_row_height(500) == kRowHeightMax && clamp_row_height(NAN) == kRowHeightNormal && clamp_row_height(30) == 30,
              "clamped to the range");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_density\":\"compact\"}}")).row_height == row_height_of_density(RowDensity::Compact), "compact -> its row height");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_density\":\"comfortable\"}}")).row_height == row_height_of_density(RowDensity::Comfortable), "comfortable -> its row height");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_density\":\"comfortable\",\"row_height\":33}}")).row_height == 33, "row_height wins over row_density");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_height\":\"tall\"}}")).row_height == kRowHeightNormal, "wrong type keeps the default");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_height\":9999}}")).row_height == kRowHeightMax, "out of range is clamped");
        GuiPrefs s;
        s.row_height = 31;
        json j = json::parse("{\"gui\":{\"ui_scale\":1.2}}");
        s.save(j);
        CHECK(j["gui"]["row_height"] == 31 && j["gui"]["row_density"] == "comfortable" && j["gui"]["ui_scale"] == 1.2, "written next to the density step: " + j["gui"].dump());
        CHECK(GuiPrefs::load(j).row_height == 31, "round trip");
        CHECK(row_height_cell_padding(kRowHeightNormal) == 2.0f && row_height_item_spacing_mult(kRowHeightNormal) == 1.0f && row_height_frame_padding_mult(kRowHeightNormal) == 1.0f,
              "normal leaves Dear ImGui's style alone (CellPadding.y 2)");
        CHECK(row_height_cell_padding(kRowHeightMin) < row_height_cell_padding(kRowHeightNormal) && row_height_cell_padding(kRowHeightNormal) < row_height_cell_padding(kRowHeightMax),
              "taller rows -> more cell padding");
        CHECK(row_height_item_spacing_mult(kRowHeightMin) < 1.0f && row_height_item_spacing_mult(kRowHeightMax) > 1.0f && row_height_item_spacing_mult(kRowHeightMax) <= 1.6f &&
                  row_height_frame_padding_mult(kRowHeightMax) <= 1.5f,
              "list and widget rows follow, capped");
    });

    run_case("preload order: the user's club, the listed players, the face chooser, tattoos, galleries, crests, managers", [&] {
        PreloadQueue q;
        // added in the wrong order on purpose
        CHECK(q.add(PreloadKind::ManagerHead, "m1") && q.add(PreloadKind::Crest, "c1") && q.add(PreloadKind::Gallery, "g1") &&
                  q.add(PreloadKind::Tattoo, "t1") && q.add(PreloadKind::FaceChooser, "f1") && q.add(PreloadKind::FaceChooser, "C:/x/p_7.dds", true) &&
                  q.add(PreloadKind::ListedMiniface, "l1") && q.add(PreloadKind::ListedMiniface, "l2") && q.add(PreloadKind::ClubMiniface, "k1"),
              "added");
        CHECK(!q.add(PreloadKind::Tattoo, "l1") && !q.add(PreloadKind::Crest, ""), "a duplicate path and an empty one are refused");
        CHECK(q.size() == 9 && q.pending() == 9 && !q.done() && q.count(PreloadKind::ListedMiniface) == 2, "counts");
        q.finalize();
        std::vector<std::string> order;
        while (PreloadQueue::Item* it = q.next_pending()) {
            order.push_back(it->path);
            q.settle(*it);
        }
        const std::vector<std::string> want = {"k1", "l1", "l2", "f1", "C:/x/p_7.dds", "t1", "g1", "c1", "m1"};
        CHECK(order == want, "order: " + [&] { std::string s; for (auto& p : order) s += p + " "; return s; }());
        CHECK(q.done() && q.settled() == 9 && q.settled(PreloadKind::FaceChooser) == 2 && q.next_pending() == nullptr, "all settled");
        CHECK(q.items()[4].file && !q.items()[3].file, "the cache file is marked");
    });

    run_case("preload order: the first screens of every gallery come before the rest of a huge list; a waiting item is revisited", [&] {
        PreloadQueue q;
        for (size_t i = 0; i < PreloadQueue::kFirstPerKind + 3; ++i) q.add(PreloadKind::ListedMiniface, "l" + std::to_string(i));
        q.add(PreloadKind::Tattoo, "t0");
        q.add(PreloadKind::Crest, "c0");
        q.finalize();
        std::vector<std::string> order;
        while (PreloadQueue::Item* it = q.next_pending()) {
            order.push_back(it->path);
            q.settle(*it);
        }
        CHECK(order.size() == PreloadQueue::kFirstPerKind + 5, "all out");
        CHECK(order[PreloadQueue::kFirstPerKind] == "t0" && order[PreloadQueue::kFirstPerKind + 1] == "c0", "tattoo and crest after the first 400 listed faces");
        CHECK(order[PreloadQueue::kFirstPerKind + 2] == "l400" && order.back() == "l402", "the rest of the list after them");
        // a Waiting item (not settled) is looked at again, the others go on
        PreloadQueue w;
        w.add(PreloadKind::Tattoo, "a");
        w.add(PreloadKind::Tattoo, "b");
        PreloadQueue::Item* first = w.next_pending();
        CHECK(first && first->path == "a", "first");
        PreloadQueue::Item* second = w.next_pending();
        CHECK(second && second->path == "b", "a left waiting, b next");
        w.settle(*second);
        PreloadQueue::Item* again = w.next_pending();
        CHECK(again && again->path == "a" && !w.done(), "a again");
        // an item added after finalize (the Players list drawn later) takes its place by kind
        PreloadQueue late;
        late.add(PreloadKind::Tattoo, "t");
        late.finalize();
        CHECK(late.add(PreloadKind::ClubMiniface, "club") && late.next_pending()->path == "club", "a late club face goes first");
    });

    run_case("preload queue: a picture refused by a full background list stays pending and unasked, comes round again, and the queue completes once it is accepted", [&] {
        PreloadQueue q;
        q.add(PreloadKind::ClubMiniface, "a");
        q.add(PreloadKind::Tattoo, "b");
        q.finalize();
        PreloadQueue::Item* a = q.next_pending();
        CHECK(a && a->path == "a", "a first");
        q.refuse(*a);
        CHECK(a->refused && !a->asked && !a->settled && q.refused() == 1 && q.pending() == 2 && !q.done(), "refused: pending, unasked, counted");
        q.refuse(*a);
        CHECK(q.refused() == 1, "counted once however often it is refused");
        PreloadQueue::Item* b = q.next_pending();
        CHECK(b && b->path == "b", "the round robin goes on to b");
        q.accept(*b);
        CHECK(b->asked && !b->refused && q.refused() == 1, "b asked");
        q.settle(*b);
        PreloadQueue::Item* again = q.next_pending();
        CHECK(again == a && !a->asked, "a comes round again, still unasked");
        q.accept(*a);
        CHECK(a->asked && !a->refused && q.refused() == 0 && !q.done(), "accepted on the retry: no longer refused, still waiting for the file");
        q.settle(*a);
        CHECK(q.done() && q.refused() == 0 && q.next_pending() == nullptr, "the queue completes");
        // settling a refused picture (it arrived by another path, or is missing) clears the count too; so does cancel
        PreloadQueue r;
        r.add(PreloadKind::Crest, "c");
        r.add(PreloadKind::Crest, "d");
        r.finalize();
        PreloadQueue::Item* c = r.next_pending();
        r.refuse(*c);
        r.settle(*c);
        CHECK(r.refused() == 0 && !r.done(), "settled while refused");
        r.refuse(*r.next_pending());
        CHECK(r.refused() == 1, "d refused");
        r.cancel();
        CHECK(r.refused() == 0 && r.done(), "cancel clears the count");
    });

    run_case("legacy files: ask_background tells a full background list (Full, asked again later) from a path already known", [&] {
        fs::path le = g_out / "legacy_full";
        fs::remove_all(le);
        LegacyImages L(le);
        CHECK(L.background_limit() == LegacyImages::kMaxBackground && LegacyImages::kMaxBackground == 20000 && !L.background_full(), "default limit");
        L.set_background_limit(2);
        const std::string a = legacy_path::player_miniface(601), b = legacy_path::player_miniface(602), c = legacy_path::player_miniface(603);
        CHECK(L.ask_background(a) == LegacyImages::Ask::Accepted && L.ask_background(b) == LegacyImages::Ask::Accepted, "two accepted");
        CHECK(L.background_full() && L.ask_background(c) == LegacyImages::Ask::Full && !L.want_background(c), "the third is refused: the list is full");
        CHECK(L.waiting_background() == 2, "the refused one is not listed");
        CHECK(L.ask_background(a) == LegacyImages::Ask::Known && L.ask_background("data/../x.dds") == LegacyImages::Ask::Known,
              "already asked / invalid: known, not refused");
        // a picture arrives: it leaves the list and the refused one fits
        fs::create_directories(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads");
        std::ofstream(L.cache_dir() / "data" / "ui" / "imgAssets" / "heads" / "p601.dds") << "x";
        L.tick(0.0);
        CHECK(L.waiting_background() == 1 && !L.background_full(), fmt("the arrived one left (%zu waiting)", L.waiting_background()));
        CHECK(L.ask_background(c) == LegacyImages::Ask::Accepted && L.waiting_background() == 2 && L.background_full(), "accepted on the retry");
        CHECK(L.ask_background(a) == LegacyImages::Ask::Known, "a picture at hand is known, not refused, even while the list is full");
        L.flush();
        const std::string want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.find(b) != std::string::npos && want.find(c) != std::string::npos && want.find(a) == std::string::npos, "want.txt: b and c, not a: " + want);
        L.set_background_limit(LegacyImages::kMaxBackground);
        CHECK(!L.background_full(), "limit restored");
    });

    run_case("preload cancellation: the career left (or the setting off) empties the queue; reset accepts again", [&] {
        PreloadQueue q;
        q.add(PreloadKind::ClubMiniface, "k1");
        q.add(PreloadKind::Tattoo, "t1");
        q.finalize();
        q.settle(*q.next_pending());
        q.cancel();
        CHECK(q.cancelled() && q.size() == 0 && q.settled() == 0 && q.done() && q.next_pending() == nullptr, "empty after cancel");
        CHECK(!q.add(PreloadKind::ClubMiniface, "k2") && q.size() == 0, "nothing is accepted while cancelled");
        CHECK(q.count(PreloadKind::ClubMiniface) == 0 && q.settled(PreloadKind::ClubMiniface) == 0, "per-kind counts cleared");
        q.reset();
        CHECK(!q.cancelled() && q.add(PreloadKind::ClubMiniface, "k1") && q.size() == 1, "the same path is accepted again after reset");
    });

    run_case("settings: gui.preload_images defaults to true, gui.row_density to normal; malformed values keep the defaults; round trip", [&] {
        const GuiPrefs d = GuiPrefs::load(json::object());
        CHECK(d.preload_images && d.row_density == RowDensity::Normal, "defaults");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{}}")).preload_images, "default with an empty gui object");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"preload_images\":\"no\",\"row_density\":7}}")).preload_images, "wrong types keep the defaults");
        CHECK(GuiPrefs::load(json::parse("{\"gui\":{\"row_density\":\"huge\"}}")).row_density == RowDensity::Normal, "unknown density keeps normal");
        CHECK(GuiPrefs::load(json(3)).preload_images, "not an object");
        const GuiPrefs p = GuiPrefs::load(json::parse("{\"gui\":{\"preload_images\":false,\"row_density\":\"compact\",\"ui_scale\":1.2}}"));
        CHECK(!p.preload_images && p.row_density == RowDensity::Compact, "read");
        json j = json::parse("{\"gui\":{\"ui_scale\":1.2,\"toggle_key\":119},\"callnames\":{\"language\":\"it\"}}");
        GuiPrefs s;
        s.preload_images = false;
        s.row_height = row_height_of_density(RowDensity::Comfortable);  // 2.22: the density step is the nearest step of the row height
        s.save(j);
        CHECK(j["gui"]["preload_images"] == false && j["gui"]["row_density"] == "comfortable", "written");
        CHECK(j["gui"]["ui_scale"] == 1.2 && j["gui"]["toggle_key"] == 119 && j["callnames"]["language"] == "it", "other keys kept");
        const GuiPrefs back = GuiPrefs::load(j);
        CHECK(!back.preload_images && back.row_density == RowDensity::Comfortable, "round trip");
        CHECK(row_density_from_name("compact") == RowDensity::Compact && row_density_from_name("comfortable") == RowDensity::Comfortable &&
                  std::string(row_density_name(RowDensity::Normal)) == "normal" && std::string(row_density_label(RowDensity::Compact)) == "Compact",
              "names");
        CHECK(row_density_cell_padding(RowDensity::Compact) < row_density_cell_padding(RowDensity::Normal) &&
                  row_density_cell_padding(RowDensity::Normal) < row_density_cell_padding(RowDensity::Comfortable) &&
                  row_density_cell_padding(RowDensity::Normal) == 1.0f && row_density_item_spacing(RowDensity::Normal) == 1.0f &&
                  row_density_frame_padding(RowDensity::Normal) == 1.0f,
              "normal leaves the style alone; compact < normal < comfortable");
    });
}

// ---- 2.22 G-resize: the real panel, with the mouse (the proof is a DRAG, not a flag scan) -----------------------------------------------
namespace resize_ui_test {

using namespace turbo;

static ImGuiWindow* find_window(const char* part) {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Name && std::strstr(w->Name, part) && w->Active) return w;
    return nullptr;
}
// a child's window name is "<parent>/<id>" (ID stack at its root) or "<parent>/<id>_<hash>"
static ImGuiWindow* find_child(const char* id) {
    const std::string plain = std::string("/") + id, hashed = plain + "_";
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (!w->Name || !w->Active) continue;
        const std::string n = w->Name;
        if ((n.size() >= plain.size() && n.compare(n.size() - plain.size(), plain.size(), plain) == 0) || n.find(hashed) != std::string::npos) return w;
    }
    return nullptr;
}
static ImGuiWindow* select_pane() { return find_child("##select"); }
static std::string window_names(const char* part) {  // diagnostics for a failing check
    std::string r;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Name && w->Active && std::strstr(w->Name, part)) r += std::string(r.empty() ? "" : " | ") + w->Name;
    return r.empty() ? "(none)" : r;
}
static ImGuiWindow* main_panel() { return ImGui::FindWindowByName("FC 27 LE Turbo"); }
static bool near(float a, float b, float tol = 2.5f) { return std::fabs(a - b) <= tol; }

// press at `from`, move to `to` in steps (one frame each), release; the mouse button is up again at the end
static void drag(Ui& ui, ImVec2 from, ImVec2 to, int steps = 14) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(from.x, from.y);
    ui.frames(3);
    io.AddMouseButtonEvent(0, true);
    ui.frames(3);
    for (int i = 1; i <= steps; ++i) {
        io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / float(steps), from.y + (to.y - from.y) * float(i) / float(steps));
        ui.frames(1);
    }
    ui.frames(2);
    io.AddMouseButtonEvent(0, false);
    ui.frames(3);
    io.AddMousePosEvent(960.0f, 540.0f);
    ui.frames(2);
}

static json stored_gui(const fs::path& file) {
    json j = json::parse(read_file(file), nullptr, false);
    return j.is_object() && j.contains("gui") ? j["gui"] : json::object();
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    const fs::path file = le / "turbo_output" / "gui_settings.json";
    const std::string keep_file = read_file(file);
    const GuiPrefs keep_prefs = app.prefs;
    auto fresh_settings = [&](const char* body) {
        std::error_code ec;
        fs::remove(file, ec);
        if (body) std::ofstream(file.string(), std::ios::binary) << body;
        app.load_gui_settings();
        split::reset_runtime_for_tests();
        ui.frames(4);
    };
    ImGui::GetIO().DisplaySize = ImVec2(1920.0f, 1080.0f);

    run_case("resize UI: Players > Edit a player - dragging the list / editor divider 200 px right moves it; it is saved once, per page", [&] {
        fresh_settings(nullptr);
        shell::player_edit_tab() = 0;  // Profile: the tab that draws page::frame
        CHECK(ui.open_page("Players", "Player") && app.place() == shell::place_of(shell::Page::PlayerEdit), "Edit a player is open");
        ui.frames(4);
        ImGuiWindow* sel = select_pane();
        CHECK(sel != nullptr, std::string("the select pane is drawn; connected=") + (app.connected() ? "yes" : "no") + " tab=" + std::to_string(shell::player_edit_tab()) + "; windows: " + window_names("/##"));
        if (!sel) return;
        const float w0 = sel->Size.x, sc = g_ui_scale;
        // 5.0c base-32-red: was 300 px (page::frame's default). Edit a player's Profile tab draws the Classic player list since 2.22 G-ia
        // (ui_player_profile.cpp list_and_editor), whose first-use width is min(560, 45 % of the page) = 560 px at 1920; the divider is
        // split::pane_x now, so the saved-once / per-page / clamp checks below are unchanged.
        CHECK(near(w0, 560.0f * sc), fmt("starts at the Classic list's default %.0f px (got %.0f)", 560.0f * sc, w0));
        CHECK((sel->ChildFlags & ImGuiChildFlags_ResizeX) != 0, "the select pane has ResizeX");
        const float y = sel->Pos.y + sel->Size.y * 0.5f;
        drag(ui, ImVec2(sel->Pos.x + w0 + 1.5f, y), ImVec2(sel->Pos.x + w0 + 1.5f + 200.0f, y));
        sel = select_pane();
        CHECK(sel && near(sel->Size.x, w0 + 200.0f), fmt("dragged 200 px right: %.0f -> %.0f", w0, sel ? sel->Size.x : -1.0f));
        ImGuiWindow* ed = find_child("##editor");
        CHECK(ed && sel && ed->Pos.x > sel->Pos.x + sel->Size.x - 1.0f, "the editor pane moved with it");
        ui.frames(40);
        json g = stored_gui(file);
        CHECK(g.contains("splits") && g["splits"].contains("PlayerEdit/##select") && near(g["splits"]["PlayerEdit/##select"].get<float>(), (w0 + 200.0f) / sc),
              "saved under this page's key once the drag rested: " + (g.contains("splits") ? g["splits"].dump() : std::string("no splits")));
        // not written again every frame
        std::error_code ec;
        fs::remove(file, ec);
        ui.frames(120);
        CHECK(!fs::exists(file), "no write while it just sits there");
    });

    run_case("resize UI: the divider is clamped (180 px .. 70 % of the panel) and each page keeps its own width", [&] {
        fresh_settings(nullptr);
        ui.open_page("Players", "Player");
        ui.frames(4);
        ImGuiWindow* sel = select_pane();
        if (!sel) { CHECK(false, "select pane"); return; }
        float x = sel->Pos.x + sel->Size.x + 1.5f, y = sel->Pos.y + sel->Size.y * 0.5f;
        drag(ui, ImVec2(x, y), ImVec2(x + 3000.0f, y), 20);
        sel = select_pane();
        const float panel_w = main_panel() ? main_panel()->Size.x : 1920.0f;
        CHECK(sel && sel->Size.x <= panel_w * 0.7f + 2.0f && sel->Size.x >= panel_w * 0.5f, fmt("over-drag stops at 70 %%: %.0f of %.0f", sel ? sel->Size.x : -1.0f, panel_w));
        sel = select_pane();
        x = sel->Pos.x + sel->Size.x + 1.5f;
        drag(ui, ImVec2(x, y), ImVec2(x - 3000.0f, y), 20);
        sel = select_pane();
        CHECK(sel && sel->Size.x >= 180.0f * g_ui_scale - 1.0f && sel->Size.x <= 180.0f * g_ui_scale + 3.0f, fmt("under-drag stops at 180 px: %.0f", sel ? sel->Size.x : -1.0f));
        // set a distinct width for Edit a player, look at another page, come back
        split::store().set("PlayerEdit/##select", 520.0f);
        split::reset_runtime_for_tests();
        ui.frames(3);
        CHECK(select_pane() && near(select_pane()->Size.x, 520.0f * g_ui_scale), fmt("Edit a player at its stored 520: %.0f", select_pane() ? select_pane()->Size.x : -1.0f));
        CHECK(ui.open_page("Players", "Create & clone"), "Player creation opens");
        ui.frames(4);
        CHECK(select_pane() && near(select_pane()->Size.x, 300.0f * g_ui_scale), fmt("another page starts at its own default 300: %.0f", select_pane() ? select_pane()->Size.x : -1.0f));
        CHECK(ui.open_page("Players", "Player"), "back to Edit a player");
        ui.frames(4);
        CHECK(select_pane() && near(select_pane()->Size.x, 520.0f * g_ui_scale), fmt("Edit a player is back at 520: %.0f", select_pane() ? select_pane()->Size.x : -1.0f));
        // (A child's window name carries the ID stack of its page, so it differs per page; split::pane_x does not change it: it passes the same
        // literal "##select" to BeginChild as page::frame did, and pushes no ID.)
    });

    run_case("resize UI: a restart puts the stored width back (page::frame restores it)", [&] {
        fresh_settings("{\"gui\":{\"splits\":{\"PlayerEdit/##select\":431}}}");
        CHECK(app.prefs.splits.get("PlayerEdit/##select", 0) == 431, "loaded from gui_settings.json");
        ui.open_page("Players", "Player");
        ui.frames(4);
        CHECK(select_pane() && near(select_pane()->Size.x, 431.0f * g_ui_scale), fmt("431 px after the restart: %.0f", select_pane() ? select_pane()->Size.x : -1.0f));
        fresh_settings("{\"gui\":{\"splits\":{\"PlayerEdit/##select\":99999}}}");
        ui.open_page("Players", "Player");
        ui.frames(4);
        CHECK(select_pane() && main_panel() && select_pane()->Size.x <= 0.7f * main_panel()->Size.x + 2.0f, "a damaged (huge) stored width is clamped");
    });

    run_case("resize UI: the main panel - an edge drag from maximized shrinks it, leaves maximized, and is saved; the title bar moves it; a drag on the page does not", [&] {
        fresh_settings(nullptr);
        ImGuiWindow* w = main_panel();
        CHECK(w && app.prefs.window.maximized && near(w->Size.x, 1920.0f) && near(w->Pos.x, 0.0f), "starts maximized");
        if (!w) return;
        CHECK((w->Flags & ImGuiWindowFlags_NoMove) == 0 && (w->Flags & ImGuiWindowFlags_NoResize) == 0, "maximized: NoMove / NoResize are not set");
        // a drag on empty page space does not un-maximize it
        drag(ui, ImVec2(960.0f, 1062.0f), ImVec2(700.0f, 900.0f));
        w = main_panel();
        CHECK(app.prefs.window.maximized && w && near(w->Pos.x, 0.0f) && near(w->Pos.y, 0.0f) && near(w->Size.x, 1920.0f), "a stray drag on the page leaves it maximized");
        // the right edge, dragged 400 px left
        std::error_code ec;
        fs::remove(file, ec);
        drag(ui, ImVec2(1918.0f, 500.0f), ImVec2(1518.0f, 500.0f));
        w = main_panel();
        CHECK(w && near(w->Size.x, 1520.0f, 4.0f) && near(w->Size.y, 1080.0f, 4.0f), fmt("the right edge dragged 400 px: %.0f x %.0f", w ? w->Size.x : -1.0f, w ? w->Size.y : -1.0f));
        CHECK(!app.prefs.window.maximized, "dragging an edge leaves maximized");
        ui.frames(70);
        json g = stored_gui(file);
        CHECK(g.contains("window") && g["window"].value("maximized", true) == false && g["window"].value("w", 0) >= 1500 && g["window"].value("w", 0) <= 1540,
              "saved: " + (g.contains("window") ? g["window"].dump() : std::string("no window block")));
        // the title bar moves it
        w = main_panel();
        if (w) drag(ui, ImVec2(w->Pos.x + 300.0f, w->Pos.y + 8.0f), ImVec2(w->Pos.x + 300.0f + 120.0f, w->Pos.y + 8.0f + 60.0f));
        w = main_panel();
        CHECK(w && near(w->Pos.x, 120.0f, 4.0f) && near(w->Pos.y, 60.0f, 4.0f), fmt("the title bar dragged: %.0f,%.0f", w ? w->Pos.x : -1.0f, w ? w->Pos.y : -1.0f));
        // from maximized, a title bar drag of a few pixels still leaves maximized and is saved once it rests
        fresh_settings(nullptr);
        fs::remove(file, ec);
        drag(ui, ImVec2(500.0f, 8.0f), ImVec2(520.0f, 14.0f), 4);
        ui.frames(70);
        g = stored_gui(file);
        CHECK(!app.prefs.window.maximized && g.contains("window") && g["window"].value("maximized", true) == false, "a short title-bar drag from maximized is saved as not maximized");
        fresh_settings(nullptr);
    });

    run_case("resize UI: Settings > Look and feel - the Row height slider (no second control), rows get taller live, and the status line has Refresh", [&] {
        fresh_settings(nullptr);
        ui.click("Turbo settings", "FC 27 LE Turbo", "##shell");
        ui.frames(4);
        CHECK(app.prefs.section_open("settings.look", true), "the Look and feel section is open by default on the Settings page");
        CHECK(ui.find("##rowheight") != nullptr, "the Row height slider is visible without scrolling or opening anything");
        CHECK(ui.find("List spacing") == nullptr, "the old List spacing box is gone (no duplicate control)");
        const float before = ImGui::GetStyle().CellPadding.y;
        app.prefs.row_height = kRowHeightMax;
        ui.frames(3);
        const float after = ImGui::GetStyle().CellPadding.y;
        CHECK(after > before * 3.0f && near(after, row_height_cell_padding(kRowHeightMax) * g_ui_scale, 0.6f), fmt("rows taller at the maximum: CellPadding.y %.1f -> %.1f", before, after));
        app.prefs.row_height = kRowHeightMin;
        ui.frames(3);
        CHECK(ImGui::GetStyle().CellPadding.y < before, "and tighter at the minimum");
        app.prefs.row_height = kRowHeightNormal;
        ui.frames(3);
        CHECK(near(ImGui::GetStyle().CellPadding.y, before, 0.01f), "normal puts the old style back exactly");
        CHECK(ui.find("Refresh###statusrefresh") != nullptr, "the status line has the Refresh button (v2.14 behaviour)");
        CHECK(ui.click("Refresh###statusrefresh"), "and it can be pressed");
        ui.frames(3);
    });

    // put everything back
    {
        std::ofstream(file.string(), std::ios::binary) << keep_file;
        app.prefs = keep_prefs;
        split::reset_runtime_for_tests();
        ui.frames(4);
    }
}

}  // namespace resize_ui_test
