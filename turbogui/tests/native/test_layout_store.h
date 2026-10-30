// Native tests (5.0b-B layout store; Cassio 10-29 23:00: "window resizes and column/row resizes need to be SAVED"). Brief
// turbo_dev/PITWALL/briefs/5.0b-B-persist.md step 5, core/layout_store.h, evidence turbo_dev/evidence/2026-10-30/5.0b-B/RESULTS.md.
// Each case builds its OWN Dear ImGui context (a World, the null backend), so nothing here touches the App's context except the cases of
// ui_cases(), which drive the real Settings > Layout controls on the shared App.
//   (a) a table "##lsdemo" with three Resizable columns, column 0 set to 123 px with the table API: the store's autosave puts "[Table][...]" and
//       "Width=123" in the sink text and says so in the log
//   (b) a new context + load(text): column 0 is 123 px again, and a child with ResizeX is 321 px wide (a control world without the load is not)
//   (c) a BeginChild with ImGuiChildFlags_ResizeX at 321 px: the sink text has its "[Window]" block with "Size=321,"
//   (d) the source scan that compiles on both trees: turbogui/src calls SaveIniSettingsToMemory (or ...ToDisk), the overlay hands ImGui no
//       file name and loads the store, App::draw pumps it. FAILS on 975eb6f3 (nothing in turbogui/src saved the ini; ImGui's timer wrote it)
//   (e) the four converted panes (Appearance: ##lslist, ##lspreview, ##lscards; Players: ##plist) through split::pane_x: the SplitStore round
//       trip, a real divider drag on each, the restart (gui.splits -> a fresh store -> the same widths), and the call sites in the sources
//   plus: the log is rate limited, hide saves, a failed write is retried and logged, an unchanged ini is not rewritten, Reset / Undo reset,
//   the file sink (atomic, parked, restored), and ui_cases(): Reset layout (Cancel, confirm, Undo), hide, Restore window to default.
// Cases (a)-(c) and the parts of the others that need core/layout_store.h are guarded by __has_include, so the file also compiles on a tree
// without the store (the fail-old run on 975eb6f3): there they register one failing check each. (d) and (e)'s source scan fail for real.
// Included by test_main.cpp after test_gui50_window_rect.h; ui_cases runs after gui50_window_rect_test::ui_cases, run() after its run().
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "ui/gui_prefs.h"
#include "ui/ui_split.h"

#if __has_include("core/layout_store.h")
#define TURBO_HAVE_LAYOUT_STORE 1
#endif

namespace layout_store_test {

using namespace turbo;

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
static bool near_f(float a, float b, float tol = 1.5f) { return std::fabs(a - b) <= tol; }
static fs::path turbogui_root() { return fs::path(g_lua).parent_path().parent_path().parent_path(); }  // g_lua = <root>/tests/native/gui_world.lua

// the lines of the ini block whose header line contains `part` (header included), "" when there is none
static std::string block_of(const std::string& text, const std::string& part) {
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        if (!line.empty() && line[0] == '[' && has(line, part)) {
            std::string blk = line + "\n";
            p = e + 1;
            while (p < text.size()) {
                e = text.find('\n', p);
                if (e == std::string::npos) e = text.size();
                const std::string l2 = text.substr(p, e - p);
                if (!l2.empty() && l2[0] == '[') break;
                blk += l2 + "\n";
                p = e + 1;
            }
            return blk;
        }
        p = e + 1;
    }
    return "";
}

// the header lines "[Window][...]" of an ini text
static std::vector<std::string> window_headers(const std::string& text) {
    std::vector<std::string> r;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        if (text.compare(p, 9, "[Window][") == 0) r.push_back(text.substr(p, e - p));
        p = e + 1;
    }
    return r;
}

#ifdef TURBO_HAVE_LAYOUT_STORE

// ---- a memory sink: the stored text, one backup, the log lines; writes can be made to fail
struct MemSink {
    std::string stored;
    bool has_text = false;
    int writes = 0;
    bool fail_writes = false;
    std::string bak;
    bool has_bak = false;
    std::vector<std::string> lines;
    MemSink() = default;
    MemSink(const MemSink&) = delete;
    MemSink& operator=(const MemSink&) = delete;
    LayoutStore::Sink sink() {
        LayoutStore::Sink s;
        s.label = "mem://layout.ini";
        s.read = [this](std::string& out) {
            if (!has_text) return false;
            out = stored;
            return true;
        };
        s.write = [this](const std::string& t) {
            if (fail_writes) return false;
            stored = t;
            has_text = true;
            ++writes;
            return true;
        };
        s.park = [this](const std::string& stamp) {
            if (!has_text) return std::string();
            bak = stored;
            has_bak = true;
            stored.clear();
            has_text = false;
            return std::string("mem://layout.ini.bak-") + stamp;
        };
        s.unpark = [this](const std::string&) {
            if (!has_bak) return false;
            stored = bak;
            has_text = true;
            has_bak = false;
            return true;
        };
        s.log = [this](const std::string& l) { lines.push_back(l); };
        return s;
    }
    int count(const std::string& prefix) const {
        int n = 0;
        for (const std::string& l : lines) n += l.compare(0, prefix.size(), prefix) == 0 ? 1 : 0;
        return n;
    }
    bool any(const std::string& sub) const {
        for (const std::string& l : lines)
            if (has(l, sub)) return true;
        return false;
    }
    std::string last(const std::string& prefix) const {
        std::string r;
        for (const std::string& l : lines)
            if (l.compare(0, prefix.size(), prefix) == 0) r = l;
        return r;
    }
};

// ---- what a World draws
struct Demo {
    enum Mode { Table, Panes } mode = Table;
    // Table mode
    float set_w0 = 123.0f;   // width given to column 0 at set_at (0 = never set: the width comes from the load, or the default)
    int set_at = 4;          // not before frame 3: TableSetColumnWidth needs the previous frame's column limits
    float wiggle = 0.0f;     // > 0: every 12th frame column 0 alternates between set_w0 and set_w0 + wiggle
    float child_w = 321.0f;  // first-use width of the ResizeX child (a stored size wins)
    bool with_child = true;
    float req0 = -1.0f, given0 = -1.0f, child_seen = -1.0f;

    void draw_table(int frame_no) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f));
        ImGui::Begin("##lswin", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        if (ImGui::BeginTable("##lsdemo", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            if (set_w0 > 0.0f) {
                if (frame_no == set_at) ImGui::TableSetColumnWidth(0, set_w0);
                else if (wiggle > 0.0f && frame_no > set_at && frame_no % 12 == 0) ImGui::TableSetColumnWidth(0, set_w0 + ((frame_no / 12) % 2 ? wiggle : 0.0f));
            }
            ImGui::TableHeadersRow();
            for (int r = 0; r < 2; ++r) {
                ImGui::TableNextRow();
                for (int c = 0; c < 3; ++c) {
                    ImGui::TableSetColumnIndex(c);
                    ImGui::Text("r%dc%d", r, c);
                }
            }
            if (ImGuiTable* t = ImGui::GetCurrentTable()) {
                req0 = t->Columns[0].WidthRequest;
                given0 = t->Columns[0].WidthGiven;
            }
            ImGui::EndTable();
        }
        if (with_child) {
            ImGui::BeginChild("##lschild", ImVec2(child_w, 120.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
            child_seen = ImGui::GetWindowWidth();
            ImGui::TextUnformatted("a pane with ResizeX");
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // Panes mode: the four panes this track converted, with the arguments of their real call sites (ui_looks_studio.cpp, ui_players.cpp)
    void draw_panes() {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1800.0f, 400.0f));
        ImGui::Begin("##lspw", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        split::pane_x("##lslist", 300.0f, 160.0f);
        ImGui::TextUnformatted("list");
        ImGui::EndChild();
        ImGui::SameLine();
        split::pane_x("##lspreview", 260.0f, 220.0f);
        ImGui::TextUnformatted("preview");
        ImGui::EndChild();
        ImGui::SameLine();
        split::pane_x("##lscards", 300.0f, 220.0f, 2.0f);
        ImGui::TextUnformatted("cards");
        ImGui::EndChild();
        ImGui::SameLine();
        split::pane_x("##plist", 300.0f, 300.0f);
        ImGui::TextUnformatted("players");
        ImGui::EndChild();
        ImGui::End();
    }
};

// ---- an ImGui context of its own (the null backend), restored on exit
struct World {
    ImGuiContext* prev = nullptr;
    ImGuiContext* ctx = nullptr;
    Demo demo;
    LayoutStore store;
    int frame_no = 0;
    double now = 100.0;

    explicit World(float saving_rate = 0.1f) {
        prev = ImGui::GetCurrentContext();
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.IniSavingRate = saving_rate;
        ImGui_ImplNull_Init();
    }
    ~World() {
        ImGui::SetCurrentContext(ctx);
        ImGui_ImplNull_Shutdown();
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(prev);
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void frame(bool visible = true) {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ++g_frame;  // the shared harness' item records can never match a frame of this world
        store.frame(visible, now);
        if (visible) {
            if (demo.mode == Demo::Table) demo.draw_table(frame_no);
            else demo.draw_panes();
        }
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        ++frame_no;
        now += 1.0 / 60.0;
    }
    void frames(int n, bool visible = true) {
        for (int i = 0; i < n; ++i) frame(visible);
    }
};

static ImGuiWindow* find_pane(const char* id) {
    const std::string part = std::string("/") + id + "_";
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && (w->Flags & ImGuiWindowFlags_ChildWindow) && w->Name && std::strstr(w->Name, part.c_str())) return w;
    return nullptr;
}

// press at `from`, move to `to` in steps (one frame each), release (the World's context is the current one)
static void world_drag(World& w, ImVec2 from, ImVec2 to, int steps = 14) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(from.x, from.y);
    w.frames(3);
    io.AddMouseButtonEvent(0, true);
    w.frames(3);
    for (int i = 1; i <= steps; ++i) {
        io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / float(steps), from.y + (to.y - from.y) * float(i) / float(steps));
        w.frames(1);
    }
    w.frames(2);
    io.AddMouseButtonEvent(0, false);
    w.frames(3);
    io.AddMousePosEvent(900.0f, 900.0f);
    w.frames(2);
}

// the globals the pane helper keeps, put back when a case ends
struct SplitGuard {
    SplitStore* bound = split::bound_store();
    std::string page = split::page_key();
    std::map<std::string, split::Runtime> rt = split::runtimes();
    float scale = g_ui_scale;
    ~SplitGuard() {
        split::bind(bound);
        split::set_page(page.c_str());
        split::runtimes() = rt;
        g_ui_scale = scale;
    }
};

#endif  // TURBO_HAVE_LAYOUT_STORE

// ---- pure and own-context cases
static void run() {
#ifdef TURBO_HAVE_LAYOUT_STORE
    run_case("test_layout_store (a): a column set to 123 px is saved: the sink text has [Table] and Width=123, the log says so", [&] {
        MemSink m;
        World w;
        w.store.set_sink(m.sink());
        CHECK(!w.store.load() && m.any("layout loaded: none at mem://layout.ini"), "nothing stored: 'layout loaded: none' and the defaults");
        w.frames(40);
        CHECK(near_f(w.demo.req0, 123.0f) && near_f(w.demo.given0, 123.0f), fmt("column 0 asked for 123 px: request %.1f, given %.1f", w.demo.req0, w.demo.given0));
        CHECK(m.has_text && has(m.stored, "[Table]["), "the autosave wrote a [Table] block: " + std::to_string(m.stored.size()) + " bytes");
        const std::string tbl = block_of(m.stored, "[Table][");
        CHECK(has(tbl, "Width=123"), "the table block holds Width=123: '" + tbl + "'");
        CHECK(w.store.saves_written() >= 1 && m.count("layout saved:") >= 1, fmt("the log says so: %d line(s), %d write(s)", m.count("layout saved:"), m.writes));
        w.store.save_now("probe", w.now);  // an explicit save is always logged (here "unchanged": the autosave already wrote this text)
        const std::string l = m.last("layout saved:");
        CHECK(has(l, "layout saved: mem://layout.ini " + std::to_string(m.stored.size()) + " tables=1 windows=") && has(l, "(probe"),
              "the line carries path, bytes, tables=1, windows=: '" + l + "'");
    });

    run_case("test_layout_store (b): a new context + load(text) gives column 0 123 px and the child 321 px; a world without the load does not", [&] {
        std::string saved;
        {
            MemSink m;
            World w;
            w.store.set_sink(m.sink());
            w.store.load();
            w.frames(40);
            saved = m.stored;
        }
        CHECK(has(saved, "Width=123"), "the first world saved Width=123");
        {
            MemSink m;
            m.stored = saved;
            m.has_text = true;
            World w;
            w.demo.set_w0 = 0.0f;    // never set with the table API: the width can only come from the load
            w.demo.child_w = 100.0f;  // a stored size wins over this first-use size
            w.store.set_sink(m.sink());
            CHECK(w.store.load(), "load() found the text");
            CHECK(m.any("layout loaded: mem://layout.ini " + std::to_string(saved.size()) + " tables=1"),
                  "the log says what was loaded: " + (m.lines.empty() ? std::string("(no line)") : m.lines[0]));
            w.frames(12);
            CHECK(near_f(w.demo.req0, 123.0f), fmt("column 0 WidthRequest after the load: %.1f (123 expected)", w.demo.req0));
            CHECK(near_f(w.demo.child_seen, 321.0f), fmt("the ResizeX child after the load: %.1f (321 expected, 100 asked)", w.demo.child_seen));
        }
        {
            MemSink m;
            World w;
            w.demo.set_w0 = 0.0f;
            w.demo.child_w = 100.0f;
            w.store.set_sink(m.sink());
            w.store.load();
            w.frames(12);
            CHECK(!near_f(w.demo.req0, 123.0f, 10.0f) && near_f(w.demo.child_seen, 100.0f),
                  fmt("control without the load: column %.1f, child %.1f", w.demo.req0, w.demo.child_seen));
        }
    });

    run_case("test_layout_store (c): a child with ResizeX at 321 px has its [Window] block with Size=321,", [&] {
        MemSink m;
        World w;
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(40);
        const std::string blk = block_of(m.stored, "lschild");
        CHECK(!blk.empty() && has(blk, "Size=321,"), "the child's block: '" + blk + "'");
        CHECK(has(block_of(m.stored, "[Window][##lswin]"), "Pos="), "the parent window has its block too");
    });

    run_case("test_layout_store: the autosave log is one line per 10 s; the writes are not", [&] {
        MemSink m;
        World w;
        w.demo.wiggle = 20.0f;
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(1800);  // 30 s of frames, column 0 changing every 0.2 s
        CHECK(m.writes >= 20, fmt("many writes: %d", m.writes));
        CHECK(m.count("layout saved:") >= 2 && m.count("layout saved:") <= 5, fmt("few log lines: %d for %d writes", m.count("layout saved:"), m.writes));
        CHECK(m.any("earlier saves not logged"), "a line says how many were not logged");
    });

    run_case("test_layout_store: hiding the panel saves at once, before the timer", [&] {
        MemSink m;
        World w(30.0f);  // ImGui's own timer is 30 s: only the hide can have written
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(30);
        CHECK(!m.has_text, "the timer has not fired yet");
        w.frames(1, false);  // the toggle key
        CHECK(m.has_text && has(m.stored, "Width=123"), "hidden: the file holds the width");
        CHECK(has(m.last("layout saved:"), "(hide"), "logged as a hide save: '" + m.last("layout saved:") + "'");
        const int n = m.writes;
        w.frames(5, false);
        CHECK(m.writes == n, "hidden for more frames: no more writes");
    });

    run_case("test_layout_store: a failed write is logged, tried again after 5 s and logged when it works", [&] {
        MemSink m;
        m.fail_writes = true;
        World w;
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(60);
        CHECK(!m.has_text && m.count("layout save FAILED") == 1, fmt("one failure line, nothing stored: %d", m.count("layout save FAILED")));
        m.fail_writes = false;
        w.frames(400);  // more than the 5 s retry
        CHECK(m.has_text && has(m.stored, "Width=123"), "the retry stored the layout");
        CHECK(m.count("layout saved:") >= 1, "and the recovery is logged");
    });

    run_case("test_layout_store: an unchanged ini is not written again", [&] {
        MemSink m;
        World w;
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(40);
        const int n = m.writes;
        for (int i = 0; i < 5; ++i) {
            ImGui::MarkIniSettingsDirty();
            w.frames(20);
        }
        CHECK(m.writes == n, fmt("five dirty marks with nothing new: writes %d -> %d", n, m.writes));
    });

    run_case("test_layout_store: Reset parks the file and pauses the saves; Undo puts it back and resumes", [&] {
        MemSink m;
        World w;
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(40);
        CHECK(has(m.stored, "Width=123"), "a layout is stored");
        CHECK(w.store.reset("T"), "reset");
        CHECK(!m.has_text && m.has_bak && w.store.paused() && w.store.can_undo() && w.store.parked_name() == "mem://layout.ini.bak-T",
              "the file is parked as .bak-T, the saves are paused: parked_name=" + w.store.parked_name());
        CHECK(m.any("layout reset: mem://layout.ini parked as mem://layout.ini.bak-T"), "the log says so");
        CHECK(!w.store.reset("U"), "a second reset waits for Undo");
        w.demo.set_w0 = 200.0f;
        w.demo.set_at = w.frame_no + 1;
        w.frames(60);
        CHECK(!m.has_text, "a change while paused writes nothing");
        CHECK(w.store.undo_reset(w.now), "undo");
        CHECK(m.has_text && has(m.stored, "[Table][") && !w.store.paused() && !w.store.can_undo(), "the file is back, the saves run again");
        CHECK(m.any("layout reset undone: mem://layout.ini restored"), "the log says so");
        CHECK(has(m.last("layout saved:"), "(undo reset"), "and saves once: '" + m.last("layout saved:") + "'");
        CHECK(!w.store.undo_reset(w.now), "nothing left to undo");
        w.frames(12);
        CHECK(near_f(w.demo.req0, 123.0f), fmt("the parked layout is applied again: column 0 = %.1f (123 expected, 200 was set while paused)", w.demo.req0));
    });

    run_case("test_layout_store: an unconfigured store changes nothing (ImGui's flag stays for whoever reads it)", [&] {
        World w;
        LayoutStore& s = w.store;
        CHECK(!s.configured() && !s.load() && !s.reset("x") && !s.save_now("x", 0.0), "no sink: every call is a no-op");
        w.frames(60);
        CHECK(ImGui::GetIO().WantSaveIniSettings, "ImGui's save flag is left set");
        int t = -1, win = -1;
        LayoutStore::count_blocks("[Window][A]\nPos=1,1\n\n[Table][0x1,2]\nColumn 0  Width=5\n\n[Table][0x2,1]\n\n[Window][B]\n", t, win);
        CHECK(t == 2 && win == 2, fmt("count_blocks: tables %d windows %d", t, win));
    });

    run_case("test_layout_store: the file sink writes atomically, parks and restores", [&] {
        const fs::path dir = test_tmp_dir("layout_store_disk");
        std::error_code ec;
        fs::remove_all(dir, ec);
        const fs::path p = dir / "turbo_output" / "layout.ini";
        std::vector<std::string> lg;
        LayoutStore::Sink s = LayoutStore::file_sink(p, [&](const std::string& l) { lg.push_back(l); });
        std::string t;
        CHECK(!s.read(t), "no file: read says no");
        CHECK(s.park("S1").empty(), "nothing to park");
        CHECK(s.write("[Window][A]\nSize=1,2\n\n") && fs::exists(p), "the first write creates the folder and the file");
        CHECK(!fs::exists(p.string() + ".tmp"), "no .tmp left behind");
        CHECK(s.read(t) && t == "[Window][A]\nSize=1,2\n\n", "read back");
        CHECK(s.write("v2") && s.read(t) && t == "v2" && !fs::exists(p.string() + ".tmp"), "the second write replaces it");
        const std::string name = s.park("S1");
        CHECK(!fs::exists(p) && fs::exists(name) && name.size() > 7 && name.compare(name.size() - 7, 7, ".bak-S1") == 0, "parked as .bak-S1: " + name);
        CHECK(s.unpark(name) && fs::exists(p) && !fs::exists(name) && slurp(p) == "v2", "unparked");
        const fs::path blocker = dir / "blocker";
        std::ofstream(blocker.string()) << "x";
        LayoutStore::Sink bad = LayoutStore::file_sink(blocker / "sub" / "y.ini", {});
        CHECK(!bad.write("z"), "a path under a file cannot be written: false, not a crash");
        CHECK(LayoutStore::stamp_now().size() == 15, "stamp 20261030_011542: " + LayoutStore::stamp_now());
    });
#else
    run_case("test_layout_store (a)-(c): the store exists", [&] { CHECK(false, "core/layout_store.h does not exist on this tree: nothing owns the ImGui ini text"); });
#endif

    // (d) outside the guard: the same text scan on both trees
    run_case("test_layout_store (d): turbogui/src saves the ImGui ini itself (SaveIniSettingsToMemory), the overlay gives ImGui no file, App::draw pumps the store", [&] {
        const fs::path src = turbogui_root() / "src";
        int hits = 0;
        std::string where;
        for (const auto& e : fs::recursive_directory_iterator(src)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
            const std::string t = slurp(e.path());
            if (has(t, "SaveIniSettingsToMemory") || has(t, "SaveIniSettingsToDisk")) {
                ++hits;
                where += (where.empty() ? "" : ", ") + e.path().filename().string();
            }
        }
        CHECK(hits >= 1, "a call to SaveIniSettingsToMemory / SaveIniSettingsToDisk in turbogui/src: " + (where.empty() ? std::string("none (ImGui's timer wrote the file, nothing in Turbo knew)") : where));
        const std::string overlay = slurp(src / "win" / "overlay_dx12.cpp");
        CHECK(has(overlay, "IniFilename = nullptr") && has(overlay, "layout.set_sink") && has(overlay, "layout.load"),
              "the overlay hands ImGui no file name and gives the store its sink and the first load");
        CHECK(has(slurp(src / "ui" / "app.cpp"), "layout.frame("), "App::draw pumps the store every frame");
    });

    // (e) the four converted panes
    run_case("test_layout_store (e): the four converted panes are split::pane_x calls in the sources", [&] {
        const std::string looks = slurp(turbogui_root() / "src" / "ui" / "ui_looks_studio.cpp");
        const std::string players = slurp(turbogui_root() / "src" / "ui" / "ui_players.cpp");
        for (const char* id : {"##lslist", "##lspreview", "##lscards"}) {
            CHECK(has(looks, std::string("split::pane_x(\"") + id + "\""), std::string("ui_looks_studio.cpp: split::pane_x(\"") + id + "\"...)");
            CHECK(!has(looks, std::string("BeginChild(\"") + id + "\""), std::string("ui_looks_studio.cpp: no plain BeginChild(\"") + id + "\") left");
        }
        CHECK(has(players, "split::pane_x(\"##plist\"") && !has(players, "BeginChild(\"##plist\""), "ui_players.cpp: split::pane_x(\"##plist\"...), no plain BeginChild left");
    });

    run_case("test_layout_store (e): SplitStore round trip of the four converted panes through gui.splits", [&] {
        const char* keys[] = {"LooksStudio/##lslist", "LooksStudio/##lspreview", "LooksStudio/##lscards", "PlayerEdit/##plist"};
        const float vals[] = {412.0f, 288.0f, 777.0f, 356.0f};
        GuiPrefs gp;
        for (int i = 0; i < 4; ++i) gp.splits.set(keys[i], vals[i]);
        json doc = json::object();
        gp.save(doc);
        CHECK(doc.contains("gui") && doc["gui"].contains("splits") && doc["gui"]["splits"].size() == 4,
              "gui.splits holds the four panes: " + (doc.contains("gui") ? doc["gui"].dump() : std::string("no gui block")));
        const GuiPrefs back = GuiPrefs::load(doc);
        for (int i = 0; i < 4; ++i) CHECK(back.splits.get(keys[i], 0.0f) == vals[i], std::string(keys[i]) + " comes back");
        CHECK(!back.splits.dirty, "a loaded store is not dirty");
    });

#ifdef TURBO_HAVE_LAYOUT_STORE
    run_case("test_layout_store (e): each converted pane's divider dragged 100 px is stored once it rests, and a restart brings the widths back", [&] {
        SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        split::bind(&st);
        split::set_page("LooksStudio");
        split::reset_runtime_for_tests();
        const char* ids[] = {"##lslist", "##lspreview", "##lscards", "##plist"};
        const float defaults[] = {300.0f, 260.0f, 300.0f, 300.0f};
        float after[4] = {0, 0, 0, 0};
        {
            World w;
            w.demo.mode = Demo::Panes;
            w.frames(10);
            for (int i = 0; i < 4; ++i) {
                ImGuiWindow* p = find_pane(ids[i]);
                CHECK(p != nullptr && near_f(p->Size.x, defaults[i]), fmt("%s starts at its default %.0f: %.1f", ids[i], defaults[i], p ? p->Size.x : -1.0f));
            }
            for (int i = 0; i < 4; ++i) {
                ImGuiWindow* p = find_pane(ids[i]);
                if (!p) continue;
                const float w0 = p->Size.x, x = p->Pos.x + p->Size.x + 1.5f, y = p->Pos.y + p->Size.y * 0.5f;
                world_drag(w, ImVec2(x, y), ImVec2(x + 100.0f, y));
                w.frames(40);
                p = find_pane(ids[i]);
                CHECK(p != nullptr && near_f(p->Size.x, w0 + 100.0f), fmt("%s dragged 100 px right: %.1f -> %.1f", ids[i], w0, p ? p->Size.x : -1.0f));
                after[i] = p ? p->Size.x : 0.0f;
                const std::string key = std::string("LooksStudio/") + ids[i];
                CHECK(st.has(key) && near_f(st.get(key, 0.0f), w0 + 100.0f), fmt("%s stored: %.1f", key.c_str(), st.get(key, -1.0f)));
            }
        }
        // the next start: gui_settings.json -> a fresh store -> fresh panes
        GuiPrefs gp;
        gp.splits = st;
        json doc = json::object();
        gp.save(doc);
        SplitStore st2 = GuiPrefs::load(doc).splits;
        split::bind(&st2);
        split::reset_runtime_for_tests();
        {
            World w;
            w.demo.mode = Demo::Panes;
            w.frames(10);
            for (int i = 0; i < 4; ++i) {
                ImGuiWindow* p = find_pane(ids[i]);
                CHECK(p != nullptr && near_f(p->Size.x, after[i]), fmt("restart: %s is %.1f again (was %.1f)", ids[i], p ? p->Size.x : -1.0f, after[i]));
            }
        }
    });
#endif
}

// ---- the real Settings > Layout controls on the shared App
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
#ifdef TURBO_HAVE_LAYOUT_STORE
    const GuiPrefs keep_prefs = app.prefs;
    const bool keep_visible = app.visible;
    const std::function<void(const std::string&)> keep_hook = app.log_hook;
    const float keep_rate = ImGui::GetIO().IniSavingRate;
    const std::map<std::string, float> keep_parked = app.layout_splits_parked_;
    app.visible = true;
    ImGui::GetIO().IniSavingRate = 0.1f;
    ImGui::GetCurrentContext()->SettingsDirtyTimer = 0.0f;  // an earlier case may have left a 5 s countdown running (MarkIniSettingsDirty keeps a running timer)
    MemSink m;
    app.layout.set_sink(m.sink());
    app.prefs.splits.set("TestPage/##lslist", 321.0f);

    run_case("test_layout_store (f): the real App pumps the store; Settings > Layout: Reset asks first, Cancel keeps everything, Reset parks, Undo brings back", [&] {
        ImGui::MarkIniSettingsDirty();
        for (int i = 0; i < 400 && !m.has_text; ++i) ui.frame();  // the timer is 0.1 s; a longer wait only guards against a countdown an earlier case left running
        CHECK(m.has_text && m.count("layout saved:") >= 1, "App::draw saved ImGui's ini to the sink: " + m.last("layout saved:"));
        const std::vector<std::string> before = window_headers(m.stored);
        CHECK(!before.empty(), fmt("the stored ini has window blocks: %d", int(before.size())));
        CHECK(ui.click("Turbo settings", "FC 27 LE Turbo", "##shell"), "open Turbo settings");
        ui.frames(3);
        CHECK(ui.click("###layout_reset", "FC 27 LE Turbo"), "click Reset layout");
        ui.frames(3);
        CHECK(ui.find("###layout_reset_cancel") != nullptr && ui.find("###layout_reset_go") != nullptr, "the confirm shows Cancel and Reset layout");
        CHECK(!app.layout.can_undo() && m.has_text, "nothing happened yet");
        CHECK(ui.click("###layout_reset_cancel"), "click Cancel");
        ui.frames(3);
        CHECK(!app.layout.can_undo() && m.has_text && !m.has_bak && app.prefs.splits.get("TestPage/##lslist", 0.0f) == 321.0f, "Cancel: the file and the pane widths are untouched");
        CHECK(ui.click("###layout_reset", "FC 27 LE Turbo"), "Reset layout again");
        ui.frames(3);
        CHECK(ui.click("###layout_reset_go"), "confirm");
        ui.frames(3);
        CHECK(app.layout.can_undo() && !m.has_text && m.has_bak && app.layout.paused(), "reset: the file is parked, saves paused");
        CHECK(!app.prefs.splits.has("TestPage/##lslist"), "the pane widths in gui.splits are reset too");
        CHECK(m.any("layout reset: mem://layout.ini parked as mem://layout.ini.bak-"), "logged: " + m.last("layout reset:"));
        ImGui::MarkIniSettingsDirty();
        ui.frames(30);
        CHECK(!m.has_text, "paused: another dirty mark writes nothing");
        CHECK(ui.click("###layout_undo", "FC 27 LE Turbo"), "click Undo reset");
        ui.frames(5);
        CHECK(!app.layout.can_undo() && !app.layout.paused() && m.has_text, "undone: the file is back, saves resumed");
        int missing = 0;
        std::string first_missing;
        for (const std::string& h : before)
            if (!has(m.stored, h + "\n")) {
                ++missing;
                if (first_missing.empty()) first_missing = h;
            }
        CHECK(missing == 0, fmt("every window block of the stored layout is back: %d missing %s", missing, first_missing.c_str()));
        CHECK(app.prefs.splits.get("TestPage/##lslist", 0.0f) == 321.0f, "the pane widths are back");
        CHECK(m.any("layout reset undone: mem://layout.ini restored"), "logged");
    });

    run_case("test_layout_store (f): hiding the panel saves the layout (App::draw -> LayoutStore::frame)", [&] {
        ImGui::MarkIniSettingsDirty();
        ui.frames(30);
        const int n = m.count("layout saved:");
        app.visible = false;
        ui.frames(2);
        app.visible = true;
        ui.frames(2);
        CHECK(m.count("layout saved:") == n + 1 && has(m.last("layout saved:"), "(hide"), "one hide save: '" + m.last("layout saved:") + "'");
    });

    run_case("test_layout_store (f): Restore window to default - the first-use size, not maximized, a log line", [&] {
        app.prefs.window.maximized = false;
        app.prefs.window.has_rect = true;
        app.prefs.window.rect = WindowRect{40.0f, 40.0f, 1500.0f, 900.0f};
        app.prefs.window_rt.apply_pending = true;
        ui.frames(4);
        ImGuiWindow* w = ImGui::FindWindowByName("FC 27 LE Turbo");
        CHECK(w && near_f(w->Size.x, 1500.0f, 4.0f) && near_f(w->Size.y, 900.0f, 4.0f), fmt("before: %.0fx%.0f", w ? w->Size.x : -1.0f, w ? w->Size.y : -1.0f));
        CHECK(ui.click("###layout_window", "FC 27 LE Turbo"), "click Restore window to default");
        ui.frames(5);
        w = ImGui::FindWindowByName("FC 27 LE Turbo");
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float ew = std::min(1180.0f * g_ui_scale, ds.x * 0.9f), eh = std::min(760.0f * g_ui_scale, ds.y * 0.88f);
        CHECK(w && !app.prefs.window.maximized && near_f(w->Size.x, ew, 4.0f) && near_f(w->Size.y, eh, 4.0f),
              fmt("after: %.0fx%.0f, the first-use size is %.0fx%.0f", w ? w->Size.x : -1.0f, w ? w->Size.y : -1.0f, ew, eh));
        bool logged = false;
        for (const std::string& l : app.log_lines) logged = logged || has(l, "window: restoring the default size");
        CHECK(logged, "the in-app Log says so");
    });

    app.layout.set_sink({});
    app.log_hook = keep_hook;
    ImGui::GetIO().IniSavingRate = keep_rate;
    app.layout_splits_parked_ = keep_parked;
    app.prefs = keep_prefs;
    app.prefs.window_rt = WindowTracker();
    app.visible = keep_visible;
    ui.frames(3);
#else
    (void)app;
    (void)ui;
    run_case("test_layout_store (f): the App has a layout store", [&] { CHECK(false, "core/layout_store.h does not exist on this tree"); });
#endif
}

}  // namespace layout_store_test
