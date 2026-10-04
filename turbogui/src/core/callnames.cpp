#include "callnames.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace turbo {

namespace fs = std::filesystem;

Callnames::Callnames(fs::path le_root) : root_(std::move(le_root)) {}

fs::path Callnames::lists_dir() const { return root_ / "turbo_output" / "callnames"; }

std::string Callnames::lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') { quoted = !quoted; continue; }
        if (c == sep && !quoted) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(cur);
    for (auto& s : out) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    }
    return out;
}

static bool to_int(const std::string& s, int64_t& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    long long x = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    v = x;
    return true;
}

// Header-driven CSV (comma, semicolon or tab): a `commentaryid` column feeds the generic bank, a `playerid` or
// `donor_playerid` column the real bank; a `kind` column (commentaryid|playerid) plus `id` works too. '#' = comment.
bool Callnames::parse_list_csv(const std::string& text_in, std::unordered_set<int64_t>& generic, std::unordered_set<int64_t>& real) {
    std::string text = text_in;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    std::istringstream in(text);
    std::string line;
    int col_c = -1, col_p = -1, col_kind = -1, col_id = -1;
    char sep = ',';
    bool header = false, any = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            if (line.find('\t') != std::string::npos) sep = '\t';
            else if (line.find(';') != std::string::npos && line.find(',') == std::string::npos) sep = ';';
            std::vector<std::string> h = split(line, sep);
            for (size_t i = 0; i < h.size(); ++i) {
                std::string k = lower(h[i]);
                if (k == "commentaryid") col_c = static_cast<int>(i);
                else if (k == "playerid" || k == "donor_playerid") col_p = static_cast<int>(i);
                else if (k == "kind") col_kind = static_cast<int>(i);
                else if (k == "id") col_id = static_cast<int>(i);
            }
            header = true;
            if (col_c < 0 && col_p < 0 && !(col_kind >= 0 && col_id >= 0)) return false;
            continue;
        }
        std::vector<std::string> f = split(line, sep);
        int64_t v = 0;
        if (col_kind >= 0 && col_id >= 0 && col_kind < static_cast<int>(f.size()) && col_id < static_cast<int>(f.size())) {
            std::string k = lower(f[static_cast<size_t>(col_kind)]);
            if (to_int(f[static_cast<size_t>(col_id)], v)) {
                if (k == "commentaryid") { generic.insert(v); any = true; }
                else if (k == "playerid") { real.insert(v); any = true; }
            }
        }
        if (col_c >= 0 && col_c < static_cast<int>(f.size()) && to_int(f[static_cast<size_t>(col_c)], v) && v > 0) { generic.insert(v); any = true; }
        if (col_p >= 0 && col_p < static_cast<int>(f.size()) && to_int(f[static_cast<size_t>(col_p)], v) && v > 0) { real.insert(v); any = true; }
    }
    return any;
}

bool Callnames::parse_commentary(const std::string& text, std::unordered_map<int64_t, std::string>& out) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("#turbo-commentary ", 0) != 0) return false;
    long long want = -1;
    {
        std::istringstream h(line.substr(18));
        std::string session;
        h >> session >> want;
    }
    long long n = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        int64_t id = 0;
        if (!to_int(line.substr(0, tab), id)) continue;
        out[id] = line.substr(tab + 1);
        ++n;
    }
    return want < 0 || n == want;
}

CallnameLang& Callnames::lang(const std::string& code) {
    for (auto& l : langs_)
        if (l.code == code) return l;
    CallnameLang l;
    l.code = code;
    langs_.push_back(l);
    return langs_.back();
}

void Callnames::scan() {
    std::error_code ec;
    for (auto& l : langs_) l.installed = false;
    // commentary packs in the game folder: <game>\commentary\commentaryfull_<lang>.toc, Data\Win32\commentaryfull_<lang>.toc
    if (!game_dir_.empty()) {
        for (const fs::path& dir : {game_dir_ / "commentary", game_dir_ / "Data" / "Win32", game_dir_ / "Patch" / "Win32"}) {
            if (!fs::is_directory(dir, ec)) continue;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                std::string n = lower(it->path().filename().string());
                const std::string pre = "commentaryfull_";
                if (n.rfind(pre, 0) != 0 || n.size() < pre.size() + 5 || n.substr(n.size() - 4) != ".toc") continue;
                lang(n.substr(pre.size(), n.size() - pre.size() - 4)).installed = true;
            }
        }
    }
    // per-language lists: turbo_output\callnames\<lang>*.csv
    fs::path ld = lists_dir();
    if (fs::is_directory(ld, ec)) {
        std::map<std::string, std::vector<fs::path>> by_lang;
        for (fs::directory_iterator it(ld, ec), end; !ec && it != end; it.increment(ec)) {
            std::string n = lower(it->path().filename().string());
            if (n.size() < 5 || n.substr(n.size() - 4) != ".csv") continue;
            std::string code = n.substr(0, n.size() - 4);
            size_t dot = code.find('.');
            if (dot != std::string::npos) code = code.substr(0, dot);
            size_t us = code.find('_');
            if (us != std::string::npos) {
                size_t us2 = code.find('_', us + 1);
                if (us2 != std::string::npos) code = code.substr(0, us2);  // por_br.generic -> por_br, por_br_real -> por_br
            }
            by_lang[code].push_back(it->path());
        }
        for (auto& kv : by_lang) {
            CallnameLang& l = lang(kv.first);
            if (l.files == kv.second && l.has_list) continue;  // unchanged set of files: keep (re-read on Refresh)
            l.generic.clear();
            l.real.clear();
            l.files = kv.second;
            l.has_list = false;
            for (const auto& f : kv.second) {
                std::string text;
                if (read_text(f, text) && parse_list_csv(text, l.generic, l.real)) l.has_list = true;
            }
        }
    }
    // commentary text from Turbo's Lua side
    fs::path cp = root_ / "turbo_output" / "bridge_commentary.txt";
    auto ct = fs::last_write_time(cp, ec);
    if (!ec && (!have_commentary_time_ || ct != commentary_time_)) {
        std::string text;
        std::unordered_map<int64_t, std::string> m;
        if (read_text(cp, text) && parse_commentary(text, m)) {
            commentary_text_ = std::move(m);
            commentary_time_ = ct;
            have_commentary_time_ = true;
        }
    }
    std::sort(langs_.begin(), langs_.end(), [](const CallnameLang& a, const CallnameLang& b) { return a.code < b.code; });
}

void Callnames::index_db(Database& db) {
    name_commentary_.clear();
    player_commentary_.clear();
    commentary_known_.clear();
    indexed_ = false;
    if (const Table* t = db.table("playernames")) {
        if (t->has("nameid") && t->has("commentaryid")) {
            Snapshot s;
            if (s.load(db.memory(), *t)) {
                for (uint32_t i : s.valid) name_commentary_[s.get_int(i, "nameid")] = s.get_int(i, "commentaryid");
                indexed_ = true;
            }
        }
    }
    if (const Table* t = db.table("playernamemap")) {
        if (t->has("playerid") && t->has("commentaryid")) {
            Snapshot s;
            if (s.load(db.memory(), *t))
                for (uint32_t i : s.valid) {
                    int64_t cid = s.get_int(i, "commentaryid");
                    if (cid > 0) player_commentary_[s.get_int(i, "playerid")] = cid;
                }
        }
    }
    if (const Table* t = db.table("commentarynames")) {
        if (t->has("commentaryid")) {
            Snapshot s;
            if (s.load(db.memory(), *t))
                for (uint32_t i : s.valid) commentary_known_.insert(s.get_int(i, "commentaryid"));
        }
    }
}

const CallnameLang* Callnames::active() const {
    const CallnameLang* first_installed = nullptr;
    for (const auto& l : langs_) {
        if (l.code == active_ && !active_.empty()) return &l;
        if (l.installed && !first_installed) first_installed = &l;
    }
    if (first_installed) return first_installed;
    return langs_.empty() ? nullptr : &langs_.front();
}

std::vector<std::string> Callnames::installed_codes() const {
    std::vector<std::string> out;
    for (const auto& l : langs_)
        if (l.installed) out.push_back(l.code);
    return out;
}

bool Callnames::generic_spoken(int64_t cid) const {
    if (cid <= kNoCommentary) return false;
    const CallnameLang* L = active();
    if (L && L->has_list) return L->generic.count(cid) > 0;
    return known(cid);
}

bool Callnames::real_spoken(int64_t pid) const {
    const CallnameLang* L = active();
    return L && L->has_list && L->real.count(pid) > 0;
}

std::string Callnames::commentary_text(int64_t cid) const {
    auto it = commentary_text_.find(cid);
    return it == commentary_text_.end() ? std::string() : it->second;
}

int64_t Callnames::commentaryid_of_name(int64_t nameid) const {
    auto it = name_commentary_.find(nameid);
    return it == name_commentary_.end() ? 0 : it->second;
}

int64_t Callnames::commentaryid_of_player(int64_t pid) const {
    auto it = player_commentary_.find(pid);
    return it == player_commentary_.end() ? 0 : it->second;
}

CallnameInfo Callnames::info(int64_t playerid, int64_t commonnameid, int64_t lastnameid) const {
    CallnameInfo i;
    if (commonnameid > 0) {
        i.binding_nameid = commonnameid;
        i.binding_field = "commonnameid";
    } else {
        i.binding_nameid = lastnameid;
        i.binding_field = "lastnameid";
    }
    int64_t cid = commentaryid_of_player(playerid);
    if (cid > 0) i.mapped = true;
    else cid = commentaryid_of_name(i.binding_nameid);
    i.commentaryid = cid;
    const CallnameLang* L = active();
    i.unverified = !(L && L->has_list);
    i.generic_spoken = generic_spoken(cid);
    i.real_spoken = real_spoken(playerid);
    i.text = commentary_text(cid);
    return i;
}

}  // namespace turbo
