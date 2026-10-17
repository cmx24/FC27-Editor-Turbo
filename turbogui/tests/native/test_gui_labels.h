// Native tests (2.16 GUI revamp step 0, brief 2.16-gui-revamp, plan TURBO_GUI_REVAMP_PLAN.md sections 5.1 R9/R10 and 6): two guards on the
// words and ids of src/ui, read from the source text (gui_label_scan.h) so that no page has to be reachable in a test frame.
//
//  1. THE ID MANIFEST (R10). Dear ImGui hashes a widget's label into its id; "Label##x" and "Label" are different ids, and a label
//     written "Label###x" keeps the id of "###x" whatever the text before it says. A renamed label that has no "###" gets a new id:
//     what ImGui stored under the old one (window and table layout in imgui.ini, tab and popup state) is lost. tests/native/
//     gui_labels.golden.txt lists every id-bearing label of src/ui (file, ImGui call, id key, count). The case fails when an entry of
//     that list is gone or occurs less often: a rename, a delete, or a label moved to another call. A NEW label passes (parallel
//     tracks add buttons); it is guarded after the next update. A "###x" id used twice in one file fails (a collision).
//     To rename a label on purpose: first write it "Old text###stable_id" (the id changes once, here), later renames of its text
//     change nothing. After an intended id change run the update (below), read the diff of the list, and commit it.
//  2. THE WORD GATE (R9). The main pages must not show paths, file names, Lua, hooks, channels, JSON, md5 ... to a player. Every prose
//     literal of src/ui (one with a space; log lines excluded) is searched for those words. tests/native/gui_words_baseline.txt
//     holds, per file and word, how many literals contain it today; the case fails when a count GOES UP. A file listed as "exempt" is
//     Advanced only (raw tables, experiments, match setup) and is not counted. The baseline only ever shrinks: a step that cleans
//     words lowers its numbers with the update, and the diff of the file is the proof.
//
// Both files are regenerated with TURBO_GUI_LABELS_UPDATE=1 (any value) in the environment of the native run, for example
//   bash scripts/msys.sh 'TURBO_GUI_LABELS_UPDATE=1 TURBO_TEST_ONLY="gui labels" bash turbogui/tests/native/run_native.sh'  (set inside the quotes: msys.sh does not pass the caller's environment)
// Every run also writes the full listing (every id label and every prose literal, with file and line) to <out>/gui_labels.txt and the
// word findings to <out>/gui_words.txt. Included by test_main.cpp after test_shell.h; gui_labels_test::run() runs the pure cases.
#pragma once
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gui_label_scan.h"

namespace gui_labels_test {

using namespace guilabels;

static fs::path root_dir() { return fs::path(g_lua).parent_path().parent_path().parent_path(); }  // g_lua = <root>/tests/native/gui_world.lua
static fs::path golden_path() { return root_dir() / "tests" / "native" / "gui_labels.golden.txt"; }
static fs::path baseline_path() { return root_dir() / "tests" / "native" / "gui_words_baseline.txt"; }
static bool update_mode() { return std::getenv("TURBO_GUI_LABELS_UPDATE") != nullptr; }

static std::vector<std::string> split_tabs(const std::string& s) {
    std::vector<std::string> v;
    size_t b = 0;
    for (;;) {
        const size_t e = s.find('\t', b);
        v.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
        if (e == std::string::npos) break;
        b = e + 1;
    }
    return v;
}

static std::vector<std::string> data_lines(const fs::path& p, bool* exists = nullptr) {
    std::vector<std::string> out;
    std::ifstream f(p, std::ios::binary);
    if (exists) *exists = f.good();
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line);
    }
    return out;
}

static std::string clip(const std::string& s, size_t n = 120) { return s.size() <= n ? s : s.substr(0, n) + "..."; }

static void write_text(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

// ---- 1. the id manifest
static std::string golden_text(const std::map<std::string, int>& m) {
    std::string t =
        "# GUI id manifest (tests/native/test_gui_labels.h). One line per id-bearing label of src/ui: file, ImGui call (\"-\" = a label\n"
        "# inside a longer expression), id key, count. The id key is the whole label as written, or \"###x\" when the label has \"###\".\n"
        "# The test fails when a line of this file is gone from the sources (the id changed or the widget was deleted). Renaming a\n"
        "# label: write it \"Text###stable_id\" first, then change the text. After an intended id change, regenerate with\n"
        "# TURBO_GUI_LABELS_UPDATE=1 and commit the diff of this file with the change that caused it.\n";
    for (const auto& kv : m) t += kv.first + "\t" + std::to_string(kv.second) + "\n";
    return t;
}

// ---- 2. the word gate
struct Baseline {
    std::set<std::string> exempt;
    std::map<std::string, std::string> why;                  // exempt file -> reason
    std::map<std::string, std::map<std::string, int>> n;    // file -> word -> count
};

static Baseline read_baseline(bool* exists) {
    Baseline b;
    for (const std::string& line : data_lines(baseline_path(), exists)) {
        const std::vector<std::string> f = split_tabs(line);
        if (f.size() >= 2 && f[0] == "exempt") {
            b.exempt.insert(f[1]);
            b.why[f[1]] = f.size() >= 3 ? f[2] : "";
        } else if (f.size() == 3) {
            b.n[f[0]][f[1]] = std::atoi(f[2].c_str());
        }
    }
    return b;
}

static std::string baseline_text(const Baseline& keep, const std::map<std::string, std::map<std::string, std::vector<std::string>>>& found) {
    std::string t =
        "# GUI word gate baseline (tests/native/test_gui_labels.h, plan rule R9): how many prose literals of each src/ui file still contain\n"
        "# a developer word (a path, Lua, a hook, a channel, JSON, a .txt switch file, md5 ...). The test fails when a number goes UP; a\n"
        "# step that rewrites such words lowers its numbers (TURBO_GUI_LABELS_UPDATE=1) and the diff of this file is the proof.\n"
        "# \"exempt\" = a file that only draws Advanced pages (raw tables, experiments): not counted.\n";
    for (const std::string& f : keep.exempt) t += "exempt\t" + f + "\t" + (keep.why.count(f) ? keep.why.at(f) : "") + "\n";
    for (const auto& file : found) {
        if (keep.exempt.count(file.first)) continue;
        for (const auto& w : file.second) t += file.first + "\t" + w.first + "\t" + std::to_string(w.second.size()) + "\n";
    }
    return t;
}

static void write_listing(const Scan& sc, const std::map<std::string, std::map<std::string, std::vector<std::string>>>& words) {
    std::string a = "# every id-bearing label and every prose literal of src/ui: file:line  call  literal as written\n";
    for (const Lit& l : sc.lits) {
        if (!is_id_label(l) && !is_prose(l)) continue;
        a += l.file + ":" + std::to_string(l.line) + "\t" + (l.callee.empty() ? "-" : l.callee) + "\t" + (is_id_label(l) ? "ID\t" : "text\t") + l.raw + "\n";
    }
    write_text(g_out / "gui_labels.txt", a);
    std::string w = "# prose literals that contain a developer word (R9), per file\n";
    for (const auto& f : words)
        for (const auto& kv : f.second)
            for (const std::string& raw : kv.second) w += f.first + "\t" + kv.first + "\t" + raw + "\n";
    write_text(g_out / "gui_words.txt", w);
}

static void run() {
    run_case("gui labels: the scanner reads literals, calls, ids and words (synthetic source)", [&] {
        const std::string src =
            "#include \"not_a_label.h\"\n"
            "// ImGui::Button(\"in a line comment##c\");\n"
            "/* ImGui::Button(\"in a block comment##b\"); */\n"
            "void f() {\n"
            "  if (ImGui::Button(\"Plain\")) {}\n"
            "  ImGui::Checkbox(\"Text###stable\", &v);\n"
            "  ImGui::Button(cond ? \"Yes##y\" : \"No##n\");\n"
            "  ImGui::TextDisabled(\"Open %s\\\\turbo_output\\\\x.json in the Lua hook\", p);\n"
            "  ImGui::TextWrapped(\"two \"\n"
            "                     \"pieces\");  // joined\n"
            "  log(\"a log line with channel words\");\n"
            "  ImGui::InputTextWithHint(\"##id\", \"a hint\", buf, 8);\n"
            "  char c = '\"'; ImGui::Button(\"After quote char\");\n"
            "}\n";
        std::string err;
        const std::vector<Lit> v = scan_source("syn.cpp", src, &err);
        CHECK(err.empty(), "no scanner error: " + err);
        std::set<std::string> raws;
        for (const Lit& l : v) raws.insert(l.raw);
        CHECK(raws.count("in a line comment##c") == 0 && raws.count("in a block comment##b") == 0 && raws.count("not_a_label.h") == 0,
              "comments and #include lines are skipped");
        const std::map<std::string, int> m = id_manifest(v);
        CHECK(m.count("syn.cpp\tButton\tPlain") == 1, "first argument of Button is an id label");
        CHECK(m.count("syn.cpp\tCheckbox\t###stable") == 1, "a ### label is keyed by its ### part");
        CHECK(m.count("syn.cpp\t-\tYes##y") == 1 && m.count("syn.cpp\t-\tNo##n") == 1, "## literals inside a ternary are found with callee -");
        CHECK(m.count("syn.cpp\tInputTextWithHint\t##id") == 1, "an invisible id");
        CHECK(m.count("syn.cpp\tButton\tAfter quote char") == 1, "a quote inside a character literal does not open a string");
        CHECK(m.count("syn.cpp\tTextDisabled\tOpen %s\\\\turbo_output\\\\x.json in the Lua hook") == 0, "a text call is not an id");
        bool joined = false;
        for (const Lit& l : v) joined = joined || l.text == "two pieces";
        CHECK(joined, "adjacent literals are joined");
        const auto w = word_findings(v);
        const auto& found = w.at("syn.cpp");
        CHECK(found.count("path") && found.count("turbo_output") && found.count("json") && found.count("lua") && found.count("hook"),
              "the words of the text literal are found: path, turbo_output, json, lua, hook");
        CHECK(!found.count("channel"), "a log line is not prose");
        CHECK(bad_words_in("Lua's hooks, the Channels").size() == 3 && bad_words_in("Evaluate the pool").empty(), "words are whole tokens (evaluate is not lua)");
    });

    const fs::path ui = root_dir() / "src" / "ui";
    const Scan sc = scan_folder(ui);
    run_case("gui labels: src/ui is readable", [&] {
        CHECK(fs::is_directory(ui) && sc.files > 40 && sc.lits.size() > 5000, fmt("the sources: %d files, %zu literals (%s)", sc.files, sc.lits.size(), ui.string().c_str()));
        CHECK(sc.error.empty(), "the scanner reads every file: " + sc.error);
    });
    if (sc.lits.empty()) return;
    const std::map<std::string, int> cur = id_manifest(sc.lits);
    const auto words = word_findings(sc.lits);
    write_listing(sc, words);

    run_case("gui labels: no id of the manifest changed (R10: rename a label only as Text###stable_id)", [&] {
        if (update_mode()) {
            write_text(golden_path(), golden_text(cur));
            std::printf("  UPDATED %s (%zu ids)\n", golden_path().string().c_str(), cur.size());
        }
        bool have = false;
        std::map<std::string, int> golden;
        for (const std::string& line : data_lines(golden_path(), &have)) {
            const std::vector<std::string> f = split_tabs(line);
            if (f.size() == 4) golden[f[0] + "\t" + f[1] + "\t" + f[2]] = std::atoi(f[3].c_str());
        }
        CHECK(have && golden.size() > 500, fmt("the manifest %s is there (%zu ids)", golden_path().string().c_str(), golden.size()));
        std::vector<std::string> gone, fewer, added;
        for (const auto& kv : golden) {
            const auto it = cur.find(kv.first);
            if (it == cur.end()) gone.push_back(kv.first);
            else if (it->second < kv.second) fewer.push_back(kv.first);
        }
        for (const auto& kv : cur)
            if (!golden.count(kv.first)) added.push_back(kv.first);
        std::string why;
        auto show = [&](const char* what, const std::vector<std::string>& v) {
            if (v.empty()) return;
            why += std::string("\n    ") + what + " (" + std::to_string(v.size()) + "):";
            for (size_t i = 0; i < v.size() && i < 12; ++i) {
                std::string k = v[i];
                for (char& c : k)
                    if (c == '\t') c = '|';
                why += "\n      " + clip(k, 150);
            }
        };
        show("id no longer in the sources: renamed or deleted", gone);
        show("occurs less often than before", fewer);
        if (!gone.empty() || !fewer.empty()) show("new ids (one of them may be the renamed label)", added);
        CHECK(gone.empty() && fewer.empty(),
              "a widget id changed. Write a renamed label as \"Text###stable_id\" first (the id changes once), or, if the id change is intended, run the "
              "update (TURBO_GUI_LABELS_UPDATE=1) and commit the manifest diff." + why);
        if (!added.empty()) std::printf("  note: %zu id(s) are not in the manifest yet (new labels); run the update to guard them\n", added.size());
    });

    run_case("gui labels: a ### id is unique within its file (two widgets with one id answer each other's clicks)", [&] {
        std::vector<std::string> dup;
        for (const auto& kv : cur) {
            const std::vector<std::string> f = split_tabs(kv.first);
            if (f.size() == 3 && f[2].compare(0, 3, "###") == 0 && kv.second > 1) dup.push_back(f[0] + " " + f[2] + " x" + std::to_string(kv.second));
        }
        std::string d;
        for (const std::string& s : dup) d += " " + s + ";";
        CHECK(dup.empty(), "### ids used more than once in a file:" + d);
    });

    run_case("gui labels: R9 word gate - the main pages show no more paths, Lua, hooks, channels or json than the baseline", [&] {
        if (update_mode()) {
            bool had = false;
            Baseline keep = read_baseline(&had);
            if (!had) {
                for (const char* f : {"ui_database.cpp", "ui_match.cpp", "ui_tactics.cpp", "ui_tuning.cpp", "ui_competitions.cpp"}) keep.exempt.insert(f);
                keep.why["ui_database.cpp"] = "Advanced > Database, Raw rows";
                keep.why["ui_match.cpp"] = "Advanced > Match setup";
                keep.why["ui_tactics.cpp"] = "Advanced > Tactics (experimental)";
                keep.why["ui_tuning.cpp"] = "Advanced > Gameplay tuning";
                keep.why["ui_competitions.cpp"] = "legacy Competitions panel, reachable from Advanced only";
            }
            write_text(baseline_path(), baseline_text(keep, words));
            std::printf("  UPDATED %s\n", baseline_path().string().c_str());
        }
        bool have = false;
        const Baseline b = read_baseline(&have);
        CHECK(have && !b.exempt.empty(), "the baseline " + baseline_path().string() + " is there");
        for (const std::string& f : b.exempt) CHECK(fs::exists(ui / f), "exempt file exists: " + f);
        size_t lowered = 0;
        for (const auto& file : words) {
            if (b.exempt.count(file.first)) continue;
            for (const auto& w : file.second) {
                const auto fi = b.n.find(file.first);
                const int base = fi != b.n.end() && fi->second.count(w.first) ? fi->second.at(w.first) : 0;
                if (int(w.second.size()) > base) {
                    std::string lits;
                    for (size_t i = 0; i < w.second.size() && i < 6; ++i) lits += "\n      " + clip(w.second[i], 140);
                    CHECK(false, file.first + " shows the developer word '" + w.first + "' in " + std::to_string(w.second.size()) +
                                     " text(s), the baseline allows " + std::to_string(base) +
                                     ". Say it in plain words (a path, a switch file, Lua, a hook, a channel, json belong in Advanced > Diagnostics, not on a "
                                     "main page), or, for an Advanced-only file, add it as exempt. Texts with the word:" + lits);
                } else if (int(w.second.size()) < base) {
                    ++lowered;
                }
            }
        }
        for (const auto& file : b.n) {
            if (words.count(file.first)) continue;
            for (const auto& w : file.second) lowered += w.second > 0 ? 1 : 0;
        }
        if (lowered) std::printf("  note: %zu word count(s) are below the baseline: run the update to lock the improvement in\n", lowered);
    });
}

}  // namespace gui_labels_test
