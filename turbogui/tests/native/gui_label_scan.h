// GUI revamp step 0 (brief 2.16-gui-revamp, plan TURBO_GUI_REVAMP_PLAN.md section 6): a scanner for the string literals of
// src/ui. Pure C++17 with no ImGui and no test framework, so test_gui_labels.h (the guards) and a scratch driver can share it.
//
// What it finds: every string literal of the sources with the call it sits in. From that it derives
//   - the id-bearing labels: any literal that contains "##" (an ImGui id marker, wherever it is used), and the first argument of an
//     ImGui call that takes a label or id (Button, Checkbox, BeginTabItem, CollapsingHeader, BeginPopupModal, PushID ...).
//     Dear ImGui hashes the whole label into the widget id ("Label##x" -> hash of "Label##x"), except that everything before the
//     last "###" is left out ("Label###x" -> hash of "###x"). So a label that is renamed without "###" gets a new id, which resets
//     what ImGui stored under the old one (window and table layout in imgui.ini, popup and tab state). The id key of a literal is
//     "###x" when it has "###", else the whole literal.
//   - the words a player reads: every prose literal (one with a space) that is not a log line, for the R9 word gate.
//
// Limits (on purpose): a label built at run time (std::string + number, a ternary) is seen only through its literal pieces that
// contain "##"; raw strings are refused; macros are read as plain text. The scan is text based, not a parse.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace guilabels {

namespace fs = std::filesystem;

struct Lit {
    std::string file;    // base name: "app.cpp"
    int line = 0;
    std::string callee;  // the innermost call the literal sits in ("Button", "SetTooltip", "notify"), "" outside any call
    bool first_arg = false;  // the literal is the first thing after the call's '('
    std::string raw;     // as written in the source (escapes kept), adjacent literals joined
    std::string text;    // unescaped
};

// ---- ImGui functions whose first argument is a label or an id (the label is hashed into the widget id)
inline const std::set<std::string>& id_callees() {
    static const std::set<std::string> s = {
        "Begin", "BeginChild", "BeginChildFrame", "BeginPopup", "BeginPopupModal", "OpenPopup", "BeginTable", "BeginTabBar",
        "BeginTabItem", "BeginCombo", "BeginMenu", "BeginListBox", "MenuItem", "Button", "SmallButton", "InvisibleButton",
        "ArrowButton", "Checkbox", "CheckboxFlags", "RadioButton", "Selectable", "TreeNode", "TreeNodeEx", "CollapsingHeader",
        "InputText", "InputTextWithHint", "InputTextMultiline", "InputInt", "InputFloat", "InputDouble", "SliderInt", "SliderFloat",
        "DragInt", "DragFloat", "DragFloatRange2", "DragIntRange2", "ColorEdit3", "ColorEdit4", "ColorButton", "Combo", "ListBox",
        "PushID", "checkbox_right"};
    return s;
}

// ---- calls whose literals are log lines or file names, not words a player reads
inline const std::set<std::string>& log_callees() {
    static const std::set<std::string> s = {"log", "log_hook", "logf", "printf", "fprintf", "fopen", "exists", "path", "filesystem"};
    return s;
}

// ---- the words the main pages must not show (R9). Matched on lower-case tokens ([a-z0-9]+), a trailing plural 's' ignored.
inline const std::vector<std::string>& bad_words() {
    static const std::vector<std::string> v = {"lua", "channel", "hook", "json", "txt", "md5", "ebx", "dll", "exe", "turbo_output", "path", "hex"};
    return v;
}

namespace detail {

inline bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// the source form of a literal -> its value (the escapes the sources use; anything else is kept as written)
inline std::string unescape(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out.push_back(raw[i]);
            continue;
        }
        const char n = raw[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case '\'': out.push_back('\''); break;
            case '0': out.push_back('\0'); break;
            default:
                out.push_back('\\');
                out.push_back(n);
        }
    }
    return out;
}

struct Frame {
    std::string callee;
    bool fresh = true;  // nothing but whitespace since the opening bracket
};

}  // namespace detail

// Reads one source file. `error` gets a message when the file uses something the scanner does not read (a raw string).
inline std::vector<Lit> scan_source(const std::string& file, const std::string& s, std::string* error = nullptr) {
    using namespace detail;
    std::vector<Lit> out;
    std::vector<Frame> stack;
    stack.push_back(Frame{"", true});
    int line = 1;
    bool bol = true;  // only white space so far on this line
    const size_t n = s.size();
    auto frame_used = [&] {
        if (!stack.empty()) stack.back().fresh = false;
    };
    for (size_t i = 0; i < n;) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
            bol = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {  // line comment
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {  // block comment
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') ++line;
                ++i;
            }
            i += 2;
            continue;
        }
        if (c == '#' && bol) {
            // a preprocessor line: #include "x.h" has a string that is not text; the other directives are read as code
            size_t j = i + 1;
            while (j < n && (s[j] == ' ' || s[j] == '\t')) ++j;
            if (s.compare(j, 7, "include") == 0) {
                while (i < n && s[i] != '\n') ++i;
                continue;
            }
        }
        bol = false;
        if (c == '\'') {  // character literal, or a digit separator (1'000)
            if (i > 0 && std::isxdigit(static_cast<unsigned char>(s[i - 1])) && i + 1 < n && std::isxdigit(static_cast<unsigned char>(s[i + 1]))) {
                ++i;
                continue;
            }
            ++i;
            while (i < n && s[i] != '\'') {
                if (s[i] == '\\') ++i;
                ++i;
            }
            ++i;
            frame_used();
            continue;
        }
        if (c == '"') {
            if (i > 0 && s[i - 1] == 'R' && (i < 2 || !ident_char(s[i - 2]))) {
                if (error) *error = file + ":" + std::to_string(line) + ": raw string literal (not read by the scanner)";
            }
            Lit lit;
            lit.file = file;
            lit.line = line;
            lit.callee = stack.empty() ? "" : stack.back().callee;
            lit.first_arg = !stack.empty() && stack.back().fresh;
            // one literal, or several joined (adjacent string literals), comments and white space between
            for (;;) {
                ++i;  // the opening quote
                while (i < n && s[i] != '"') {
                    if (s[i] == '\\' && i + 1 < n) {
                        lit.raw.push_back(s[i++]);
                    }
                    if (s[i] == '\n') ++line;
                    lit.raw.push_back(s[i++]);
                }
                ++i;  // the closing quote
                size_t j = i;
                for (;;) {
                    while (j < n && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                    if (j + 1 < n && s[j] == '/' && s[j + 1] == '/') {
                        while (j < n && s[j] != '\n') ++j;
                        continue;
                    }
                    if (j + 1 < n && s[j] == '/' && s[j + 1] == '*') {
                        j += 2;
                        while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) ++j;
                        j += 2;
                        continue;
                    }
                    break;
                }
                if (j < n && s[j] == '"') {
                    for (size_t k = i; k < j; ++k)
                        if (s[k] == '\n') ++line;
                    i = j;
                    continue;
                }
                break;
            }
            lit.text = unescape(lit.raw);
            out.push_back(std::move(lit));
            frame_used();
            continue;
        }
        if (c == '(' || c == '{') {
            Frame f;
            if (c == '(') {
                // the callee: the identifier right before the bracket (qualifiers dropped), unless it is a keyword
                size_t e = i;
                while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
                size_t b = e;
                while (b > 0 && ident_char(s[b - 1])) --b;
                f.callee = s.substr(b, e - b);
                static const std::set<std::string> kw = {"if", "while", "for", "switch", "return", "sizeof", "catch", "decltype", "else"};
                if (kw.count(f.callee)) f.callee.clear();
            }
            frame_used();
            stack.push_back(f);
            ++i;
            continue;
        }
        if (c == ')' || c == '}') {
            if (stack.size() > 1) stack.pop_back();
            frame_used();
            ++i;
            continue;
        }
        frame_used();
        ++i;
    }
    return out;
}

// ---- derived views

inline bool has_id_marker(const Lit& l) { return l.text.find("##") != std::string::npos; }
inline bool takes_label(const Lit& l) { return l.first_arg && id_callees().count(l.callee) > 0; }
inline bool is_id_label(const Lit& l) { return has_id_marker(l) || takes_label(l); }

// "###x" when the literal has "###", else the whole literal as written (its hash is the widget id)
inline std::string id_key(const Lit& l) {
    const size_t p = l.raw.rfind("###");
    return p != std::string::npos ? l.raw.substr(p) : l.raw;
}
inline bool has_stable_id(const Lit& l) { return l.raw.find("###") != std::string::npos; }

// what the player reads of a label: before the first "##"
inline std::string visible_part(const std::string& text) {
    const size_t p = text.find("##");
    return p == std::string::npos ? text : text.substr(0, p);
}

// A prose literal: something a player may read (has a space) and is not a log line or an include
inline bool is_prose(const Lit& l) {
    if (l.text.find(' ') == std::string::npos) return false;
    if (log_callees().count(l.callee)) return false;
    return true;
}

// which of bad_words() the literal contains
inline std::set<std::string> bad_words_in(const std::string& text) {
    std::set<std::string> found;
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find('\\') != std::string::npos) found.insert("path");
    if (lower.find("turbo_output") != std::string::npos) found.insert("turbo_output");
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : lower) {
        if (std::isalnum(static_cast<unsigned char>(c))) cur.push_back(c);
        else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    for (const std::string& t0 : tokens) {
        std::string t = t0;
        if (t.size() > 3 && t.back() == 's') t.pop_back();
        for (const char* w : {"lua", "channel", "hook", "json", "txt", "md5", "ebx", "dll", "exe"})
            if (t == w) found.insert(w);
        if (t0.size() >= 2 && t0[0] == '0' && t0[1] == 'x') found.insert("hex");
    }
    return found;
}

// ---- the whole source folder

struct Scan {
    std::vector<Lit> lits;
    std::string error;
    int files = 0;
};

inline Scan scan_folder(const fs::path& dir) {
    Scan sc;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string ext = e.path().extension().string();
        if (e.is_regular_file() && (ext == ".cpp" || ext == ".h")) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        std::vector<Lit> v = scan_source(p.filename().string(), ss.str(), &err);
        if (!err.empty() && sc.error.empty()) sc.error = err;
        sc.lits.insert(sc.lits.end(), v.begin(), v.end());
        ++sc.files;
    }
    return sc;
}

// ---- the manifest: one line per (file, callee, id key), with the number of times it occurs
//      "file<TAB>callee<TAB>key<TAB>count"; a literal that is not the label argument of an ImGui call has "-" as its callee
inline std::map<std::string, int> id_manifest(const std::vector<Lit>& lits) {
    std::map<std::string, int> m;
    for (const Lit& l : lits) {
        if (!is_id_label(l)) continue;
        const std::string callee = takes_label(l) ? l.callee : "-";
        ++m[l.file + "\t" + callee + "\t" + id_key(l)];
    }
    return m;
}

// ---- the word findings: file -> word -> the literals (as written) that contain it
inline std::map<std::string, std::map<std::string, std::vector<std::string>>> word_findings(const std::vector<Lit>& lits) {
    std::map<std::string, std::map<std::string, std::vector<std::string>>> m;
    for (const Lit& l : lits) {
        if (!is_prose(l)) continue;
        for (const std::string& w : bad_words_in(l.text)) m[l.file][w].push_back(l.raw);
    }
    return m;
}

}  // namespace guilabels
