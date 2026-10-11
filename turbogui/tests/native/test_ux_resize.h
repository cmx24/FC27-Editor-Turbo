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

    run_case("panes: the lists and split panes have a resize border (ImGuiChildFlags_ResizeX / ResizeY)", [&] {
        struct Want { const char* file; const char* id; const char* flag; };
        const Want wants[] = {
            {"ui_players.cpp", "##plist", "ResizeX"},        {"ui_teams.cpp", "##tlist", "ResizeX"},
            {"ui_teams.cpp", "##mlist", "ResizeX"},          {"ui_competitions.cpp", "##ltable", "ResizeX"},
            {"ui_faces.cpp", "##facegrid", "ResizeY"},
            {"ui_tactics.cpp", "##tacleft", "ResizeX"},      {"ui_tactics.cpp", "##taccentre", "ResizeX"},
            {"ui_images.cpp", "##tgrid", "ResizeY"},         {"ui_images.cpp", "##ggrid", "ResizeY"},
            {"ui_images.cpp", "##chooser", "ResizeX"},       {"ui_identity.cpp", "##crestgrid", "ResizeY"},
            {"ui_bodytypes.cpp", "##btlist", "ResizeY"},     {"ui_names.cpp", "##nmids", "ResizeY"},
            {"ui_standings.cpp", "##lsfix", "ResizeY"},      {"file_picker.cpp", "##fpfiles", "ResizeY"},
            {"ui_presets.cpp", "##clublist", "ResizeX"},     {"ui_cmtracker.cpp", "##cmtlist", "ResizeY"},
        };
        for (const Want& w : wants) {
            const std::string text = read_file(ux_src_ui() / w.file);
            bool found = false;
            for (const std::string& args : ux_calls(text, "ImGui::BeginChild")) {
                const std::vector<std::string> ids = ux_first_arg_literals(args);
                if (std::find(ids.begin(), ids.end(), std::string(w.id)) == ids.end()) continue;
                found = args.find(std::string("ImGuiChildFlags_") + w.flag) != std::string::npos;
            }
            CHECK(found, std::string(w.file) + " " + w.id + " has " + w.flag);
        }
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
        s.row_density = RowDensity::Comfortable;
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
