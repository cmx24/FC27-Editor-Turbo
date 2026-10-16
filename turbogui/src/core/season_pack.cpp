// FC 27 LE Turbo GUI - season update pack importer (see season_pack.h)
#include "core/season_pack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

#include "core/cmtracker.h"
#include "core/model.h"

namespace turbo {
namespace season_pack {

using nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- helpers
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// whole-text integer in 0..2^31-1 (no sign, no spaces inside); false otherwise
static bool parse_id(const std::string& text, int64_t& out) {
    std::string t = trim(text);
    if (t.empty() || t.size() > 10) return false;
    int64_t v = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v > 2147483647LL) return false;
    out = v;
    return true;
}
static bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
static std::string now_text() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

// header name -> column index, with aliases; -1 when absent
static int column(const std::vector<std::string>& head, std::initializer_list<const char*> names) {
    for (const char* n : names)
        for (size_t i = 0; i < head.size(); ++i)
            if (lower(trim(head[i])) == n) return static_cast<int>(i);
    return -1;
}
static std::string cell(const std::vector<std::string>& row, int col) {
    return col >= 0 && static_cast<size_t>(col) < row.size() ? trim(row[static_cast<size_t>(col)]) : std::string();
}
static bool blank_row(const std::vector<std::string>& row) {
    for (const auto& c : row)
        if (!trim(c).empty()) return false;
    return true;
}

// ---------------------------------------------------------------- reading
bool read_pack(const fs::path& dir, Pack& out, std::string* err) {
    out = Pack{};
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (err) *err = "not a folder: " + dir.string();
        return false;
    }
    const fs::path tfile = dir / "transfers.csv", mfile = dir / "managers.csv";
    const bool has_t = fs::is_regular_file(tfile, ec), has_m = fs::is_regular_file(mfile, ec);
    if (!has_t && !has_m) {
        if (err) *err = "no transfers.csv or managers.csv in " + dir.string();
        return false;
    }
    out.info.name = dir.filename().string();
    std::string text;
    if (fs::is_regular_file(dir / "pack.json", ec)) {
        if (read_text(dir / "pack.json", text)) {
            json j = json::parse(text, nullptr, false);
            if (j.is_object()) {
                auto get = [&](const char* k, std::string& dst) {
                    if (j.contains(k) && j[k].is_string()) dst = trim(j[k].get<std::string>());
                };
                std::string name;
                get("name", name);
                if (!name.empty()) out.info.name = name;
                get("season", out.info.season);
                get("source", out.info.source);
            } else out.problems.push_back("pack.json is not a JSON object: the folder name is the pack name");
        } else out.problems.push_back("pack.json could not be read");
    }
    if (out.info.name.size() > 80) out.info.name.resize(80);
    size_t rows_total = 0;

    if (has_t) {
        if (!read_text(tfile, text)) out.problems.push_back("transfers.csv could not be read");
        else {
            auto rows = parse_csv_text(text);
            if (rows.empty()) out.problems.push_back("transfers.csv is empty");
            else {
                const auto& h = rows[0];
                const int cp = column(h, {"playerid", "player_id", "info.playerid", "id"});
                const int cf = column(h, {"from", "from_team", "from_teamid", "from_team_id", "from_id"});
                const int ct = column(h, {"to", "to_team", "to_teamid", "to_team_id", "to_id"});
                const int cn = column(h, {"player", "name", "player_name"});
                if (cp < 0 || ct < 0) out.problems.push_back("transfers.csv needs the columns playerid and to (from is optional)");
                else {
                    for (size_t i = 1; i < rows.size(); ++i) {
                        if (blank_row(rows[i])) continue;
                        if (++rows_total > kMaxPackRows) {
                            out.problems.push_back("more than " + std::to_string(kMaxPackRows) + " rows: the rest is ignored");
                            break;
                        }
                        TransferIn t;
                        t.line = i + 1;
                        t.from_text = cell(rows[i], cf);
                        t.to_text = cell(rows[i], ct);
                        t.player_text = cell(rows[i], cn);
                        if (!parse_id(cell(rows[i], cp), t.playerid) || t.playerid <= 0) {
                            out.unreadable.push_back({"transfers.csv", t.line, "playerid '" + cell(rows[i], cp) + "' is not a player id"});
                            continue;
                        }
                        if (t.to_text.empty()) {
                            out.unreadable.push_back({"transfers.csv", t.line, "no target team"});
                            continue;
                        }
                        out.transfers.push_back(t);
                    }
                }
            }
        }
    }
    if (has_m) {
        if (!read_text(mfile, text)) out.problems.push_back("managers.csv could not be read");
        else {
            auto rows = parse_csv_text(text);
            if (rows.empty()) out.problems.push_back("managers.csv is empty");
            else {
                const auto& h = rows[0];
                const int cm = column(h, {"managerid", "manager_id", "id"});
                const int ct = column(h, {"to", "to_team", "to_teamid", "to_team_id", "team", "teamid"});
                const int cn = column(h, {"manager", "name", "manager_name"});
                if (cm < 0 || ct < 0) out.problems.push_back("managers.csv needs the columns managerid and to");
                else {
                    for (size_t i = 1; i < rows.size(); ++i) {
                        if (blank_row(rows[i])) continue;
                        if (++rows_total > kMaxPackRows) {
                            out.problems.push_back("more than " + std::to_string(kMaxPackRows) + " rows: the rest is ignored");
                            break;
                        }
                        ManagerIn m;
                        m.line = i + 1;
                        m.to_text = cell(rows[i], ct);
                        m.manager_text = cell(rows[i], cn);
                        if (!parse_id(cell(rows[i], cm), m.managerid) || m.managerid <= 0) {
                            out.unreadable.push_back({"managers.csv", m.line, "managerid '" + cell(rows[i], cm) + "' is not a manager id"});
                            continue;
                        }
                        if (m.to_text.empty()) {
                            out.unreadable.push_back({"managers.csv", m.line, "no target team"});
                            continue;
                        }
                        out.managers.push_back(m);
                    }
                }
            }
        }
    }
    if (fs::is_directory(dir / "kits", ec)) {
        for (const auto& e : fs::directory_iterator(dir / "kits", ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::string ext = lower(e.path().extension().string());
            if (ext == ".zip") out.kits.push_back(e.path().filename().string());
        }
        std::sort(out.kits.begin(), out.kits.end());
    }
    return true;
}

// ---------------------------------------------------------------- snapshot
std::map<int64_t, int> Snapshot::squad_sizes() const {
    std::map<int64_t, int> n;
    for (const auto& kv : player_club)
        if (kv.second > 0 && kv.second != kFreeAgents) ++n[kv.second];
    return n;
}
std::string Snapshot::team_label(int64_t id) const {
    if (id == 0 || id == kFreeAgents) return "free agent";
    auto it = teams.find(id);
    return it != teams.end() && !it->second.name.empty() ? it->second.name : "team " + std::to_string(id);
}
int64_t Snapshot::find_team(const std::string& text) const {
    int64_t id = 0;
    if (parse_id(text, id)) return teams.count(id) || id == 0 || id == kFreeAgents ? id : -1;
    const std::string want = lower(trim(text));
    if (want.empty()) return -1;
    if (want == "free agents" || want == "free agent") return kFreeAgents;
    int64_t found = -1;
    for (const auto& kv : teams) {
        if (lower(kv.second.name) != want) continue;
        if (found >= 0) return -1;   // two teams with that name: not unique
        found = kv.first;
    }
    return found;
}
Snapshot snapshot_from_model(const Model& m, int64_t user_team) {
    Snapshot s;
    s.user_team = user_team;
    for (const auto& t : m.teams()) {
        TeamInfo ti;
        ti.name = t.name;
        ti.national = m.is_national_team(t.teamid);
        s.teams[t.teamid] = ti;
    }
    for (const auto& p : m.players()) {
        s.player_club[p.playerid] = p.club;
        s.player_name[p.playerid] = p.name;
        if (p.on_loan) s.on_loan[p.playerid] = p.loaned_from;
    }
    for (const auto& mg : m.managers()) {
        s.manager_team[mg.managerid] = mg.teamid;
        s.manager_name[mg.managerid] = mg.name;
    }
    return s;
}

// ---------------------------------------------------------------- preview
size_t Preview::pages() const { return (accepted + kPageRows - 1) / kPageRows; }

static bool at_user(const Snapshot& s, int64_t team) { return team > 0 && team == s.user_team; }

Preview preview(const Pack& pack, const Snapshot& live) {
    Preview pv;
    pv.info = pack.info;
    pv.kits = pack.kits;
    pv.notes.push_back("Fees in the pack are ignored: no money moves.");
    pv.notes.push_back("Undo restores club links and manager teams only, not the join date, previous team or contract.");
    if (!pack.kits.empty()) pv.notes.push_back(std::to_string(pack.kits.size()) + " kit zip(s) listed only: import them in Teams > Kits.");
    for (const auto& p : pack.problems) pv.notes.push_back(p);

    for (const auto& u : pack.unreadable) {
        PreviewRow r;
        r.kind = u.file == "managers.csv" ? Kind::Manager : Kind::Transfer;
        r.file = u.file;
        r.line = u.line;
        r.reason = u.reason;
        pv.rows.push_back(r);
    }

    Snapshot w = live;                                  // working copy: a later row sees the earlier rows
    std::map<int64_t, int> squad = w.squad_sizes();
    std::map<int64_t, size_t> seen_player;              // pid -> first line
    std::map<int64_t, size_t> seen_manager;

    for (const auto& t : pack.transfers) {
        PreviewRow r;
        r.kind = Kind::Transfer;
        r.file = "transfers.csv";
        r.line = t.line;
        r.id = t.playerid;
        auto fail = [&](const std::string& why) {
            r.ok = false;
            r.reason = why;
            pv.rows.push_back(r);
        };
        auto pit = w.player_club.find(t.playerid);
        auto nit = w.player_name.find(t.playerid);
        r.subject = nit != w.player_name.end() && !nit->second.empty() ? nit->second : (!t.player_text.empty() ? t.player_text : "player " + std::to_string(t.playerid));
        if (w.user_team <= 0) { fail("your club is not known yet (the window needs the career loaded)"); continue; }
        if (pit == w.player_club.end()) { fail("player " + std::to_string(t.playerid) + " is not in the career"); continue; }
        auto dup = seen_player.find(t.playerid);
        if (dup != seen_player.end()) { fail("duplicate: this player already has a row at line " + std::to_string(dup->second)); continue; }
        seen_player[t.playerid] = t.line;
        const int64_t cur = pit->second == 0 ? kFreeAgents : pit->second;
        r.from = cur;
        auto loan = w.on_loan.find(t.playerid);
        if (loan != w.on_loan.end()) {
            fail("he is on loan at " + w.team_label(cur) + " from " + w.team_label(loan->second) + ": end the loan first (Players > Terminate loan)");
            continue;
        }
        const int64_t to = w.find_team(t.to_text);
        if (to < 0 || (to == 0)) { fail("target team '" + t.to_text + "' is not in the career"); continue; }
        r.to = to;
        if (!t.from_text.empty()) {
            const int64_t from = w.find_team(t.from_text);
            if (from < 0) { fail("from team '" + t.from_text + "' is not in the career"); continue; }
            const int64_t fromn = from == 0 ? kFreeAgents : from;
            if (fromn != cur) {
                fail("he is at " + w.team_label(cur) + " now, not at " + w.team_label(fromn));
                continue;
            }
        }
        if (to != kFreeAgents) {
            auto tt = w.teams.find(to);
            if (tt == w.teams.end()) { fail("target team " + std::to_string(to) + " is not in the career"); continue; }
            if (tt->second.national) { fail(w.team_label(to) + " is a national team: club moves only"); continue; }
        }
        if (to == kFreeAgents) { fail("a pack moves players between clubs: releasing him to the free agents is not part of a pack"); continue; }
        if (to == cur) { fail("he already plays for " + w.team_label(to)); continue; }
        if (at_user(w, cur) || at_user(w, to)) {
            fail("touches your club (" + w.team_label(w.user_team) + "): the game pays compensation for those moves, so a pack leaves them out");
            continue;
        }
        auto ft = w.teams.find(cur);
        if (cur != kFreeAgents && ft != w.teams.end() && ft->second.national) { fail("he is on a national team link: club moves only"); continue; }
        if (cur != kFreeAgents) {
            const int n = squad[cur];
            if (n >= kMinSquad && n - 1 < kMinSquad) {
                fail(w.team_label(cur) + " would have " + std::to_string(n - 1) + " players, fewer than the " + std::to_string(kMinSquad) + " a match squad needs");
                continue;
            }
        }
        if (to != kFreeAgents && squad[to] >= kMaxSquad) {
            fail(w.team_label(to) + " has " + std::to_string(squad[to]) + " players, the most a squad holds");
            continue;
        }
        // accepted: update the working copy
        if (cur != kFreeAgents) --squad[cur];
        if (to != kFreeAgents) ++squad[to];
        w.player_club[t.playerid] = to;
        r.ok = true;
        r.text = r.subject + ": " + w.team_label(cur) + " -> " + w.team_label(to);
        pv.rows.push_back(r);
    }

    for (const auto& m : pack.managers) {
        PreviewRow r;
        r.kind = Kind::Manager;
        r.file = "managers.csv";
        r.line = m.line;
        r.id = m.managerid;
        auto fail = [&](const std::string& why) {
            r.ok = false;
            r.reason = why;
            pv.rows.push_back(r);
        };
        auto mit = w.manager_team.find(m.managerid);
        auto nit = w.manager_name.find(m.managerid);
        r.subject = nit != w.manager_name.end() && !nit->second.empty() ? nit->second : (!m.manager_text.empty() ? m.manager_text : "manager " + std::to_string(m.managerid));
        if (w.user_team <= 0) { fail("your club is not known yet (the window needs the career loaded)"); continue; }
        if (mit == w.manager_team.end()) { fail("manager " + std::to_string(m.managerid) + " is not in the career"); continue; }
        auto dup = seen_manager.find(m.managerid);
        if (dup != seen_manager.end()) { fail("duplicate: this manager already has a row at line " + std::to_string(dup->second)); continue; }
        seen_manager[m.managerid] = m.line;
        const int64_t cur = mit->second;
        r.from = cur;
        const int64_t to = w.find_team(m.to_text);
        if (to < 0) { fail("target team '" + m.to_text + "' is not in the career"); continue; }
        r.to = to;
        if (to == 0 || to == kFreeAgents) { fail("a pack names the club a manager joins: making a manager a free agent is not part of a pack"); continue; }
        auto tt = w.teams.find(to);
        if (tt == w.teams.end()) { fail("target team " + std::to_string(to) + " is not in the career"); continue; }
        if (tt->second.national) { fail(w.team_label(to) + " is a national team: club managers only"); continue; }
        if (cur > 0) {
            auto ct = w.teams.find(cur);
            if (ct != w.teams.end() && ct->second.national) { fail(r.subject + " manages a national team: club managers only"); continue; }
        }
        if (to == cur) { fail(r.subject + " already manages " + w.team_label(to)); continue; }
        if (at_user(w, cur) || at_user(w, to)) { fail("touches your club (" + w.team_label(w.user_team) + "): manager moves never change your own job"); continue; }
        std::vector<int64_t> displaced;
        for (const auto& kv : w.manager_team)
            if (kv.second == to && kv.first != m.managerid) displaced.push_back(kv.first);
        if (displaced.size() > 1) { fail(w.team_label(to) + " has " + std::to_string(displaced.size()) + " managers in the career: Turbo does not swap that"); continue; }
        w.manager_team[m.managerid] = to;
        r.text = r.subject + " -> " + w.team_label(to);
        if (!displaced.empty()) {
            r.displaced = displaced[0];
            w.manager_team[displaced[0]] = cur;
            auto dn = w.manager_name.find(displaced[0]);
            r.text += "; swaps with " + (dn != w.manager_name.end() && !dn->second.empty() ? dn->second : "manager " + std::to_string(displaced[0])) +
                      " -> " + w.team_label(cur);
        }
        r.ok = true;
        pv.rows.push_back(r);
    }
    for (const auto& r : pv.rows) (r.ok ? pv.accepted : pv.rejected)++;
    return pv;
}

// ---------------------------------------------------------------- sends
static json transfer_action(int64_t pid, int64_t to) {
    if (to == kFreeAgents) return {{"action", "release"}, {"playerid", pid}};   // undo of a free signing: the game's own release
    return {{"action", "transfer"}, {"playerid", pid}, {"to_teamid", to}, {"fee", 0}, {"wage", 0}};
}
static json player_send(const std::vector<json>& actions) {
    return {{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", actions}}}};
}
// The mailbox holds kMaxSendBytes of command text (core/bridge.h kMbTextSize = 4096): a page's actions go out in as many
// player_moves sends as that needs (50 transfers are about 4 KB)
static void push_player_sends(std::vector<json>& sends, const std::vector<json>& actions) {
    std::vector<json> chunk;
    size_t bytes = 0;
    const size_t base = player_send({}).dump().size();
    for (const auto& a : actions) {
        const size_t n = a.dump().size() + 1;
        if (!chunk.empty() && base + bytes + n > kMaxSendBytes) {
            sends.push_back(player_send(chunk));
            chunk.clear();
            bytes = 0;
        }
        chunk.push_back(a);
        bytes += n;
    }
    if (!chunk.empty()) sends.push_back(player_send(chunk));
}
static json manager_send(int64_t mid, int64_t team, int64_t replacement) {
    json o = {{"enabled", true}, {"managerid", mid}, {"teamid", team}, {"confirm", true}};
    if (replacement > 0) o["replacement"] = replacement;
    return {{"op", "run"}, {"module", "manager_move"}, {"overrides", o}};
}

std::vector<Page> make_pages(const Preview& pv) {
    std::vector<Page> pages;
    std::vector<size_t> acc;
    for (size_t i = 0; i < pv.rows.size(); ++i)
        if (pv.rows[i].ok) acc.push_back(i);
    for (size_t a = 0; a < acc.size(); a += kPageRows) {
        Page pg;
        pg.rows.assign(acc.begin() + static_cast<std::ptrdiff_t>(a), acc.begin() + static_cast<std::ptrdiff_t>(std::min(acc.size(), a + kPageRows)));
        std::vector<json> actions;
        for (size_t i : pg.rows)
            if (pv.rows[i].kind == Kind::Transfer) actions.push_back(transfer_action(pv.rows[i].id, pv.rows[i].to));
        push_player_sends(pg.sends, actions);
        for (size_t i : pg.rows)
            if (pv.rows[i].kind == Kind::Manager) pg.sends.push_back(manager_send(pv.rows[i].id, pv.rows[i].to, 0));
        pages.push_back(std::move(pg));
    }
    return pages;
}

UndoSet undo_rows_of(const Preview& pv, const Page& page) {
    UndoSet s;
    s.pack = pv.info.name;
    s.season = pv.info.season;
    s.when = now_text();
    for (size_t i : page.rows) {
        const PreviewRow& r = pv.rows[i];
        UndoRow u;
        u.kind = r.kind;
        u.id = r.id;
        u.from = r.from;
        u.to = r.to;
        u.displaced = r.displaced;
        u.subject = r.subject;
        s.rows.push_back(u);
    }
    return s;
}

// ---------------------------------------------------------------- undo store
void UndoStore::add(const UndoSet& s) {
    for (auto& e : sets) {
        if (e.pack != s.pack) continue;
        e.season = s.season;
        e.when = s.when;
        e.rows.insert(e.rows.end(), s.rows.begin(), s.rows.end());
        return;
    }
    sets.push_back(s);
}
bool UndoStore::remove(const std::string& pack) {
    for (size_t i = 0; i < sets.size(); ++i)
        if (sets[i].pack == pack) {
            sets.erase(sets.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    return false;
}
const UndoSet* UndoStore::find(const std::string& pack) const {
    for (const auto& e : sets)
        if (e.pack == pack) return &e;
    return nullptr;
}

std::string undo_json(const UndoStore& s) {
    json sets = json::array();
    for (const auto& e : s.sets) {
        json rows = json::array();
        for (const auto& r : e.rows)
            rows.push_back({{"kind", r.kind == Kind::Manager ? "manager" : "player"}, {"id", r.id}, {"from", r.from}, {"to", r.to},
                            {"displaced", r.displaced}, {"subject", r.subject}});
        sets.push_back({{"pack", e.pack}, {"season", e.season}, {"when", e.when}, {"rows", rows}});
    }
    json j = {{"turbo_season_pack_undo", 1},
              {"note", "Written by Turbo's season pack importer before each page is sent. Undo restores club links and manager teams only."},
              {"sets", sets}};
    return j.dump(1, ' ', false, json::error_handler_t::replace);
}

bool parse_undo_json(const std::string& text, UndoStore& out, std::string* err) {
    out = UndoStore{};
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("turbo_season_pack_undo") || j["turbo_season_pack_undo"] != 1) {
        if (err) *err = "not a Turbo season pack undo file";
        return false;
    }
    if (!j.contains("sets") || !j["sets"].is_array()) return true;
    for (const auto& e : j["sets"]) {
        if (!e.is_object() || !e.contains("pack") || !e["pack"].is_string() || !e.contains("rows") || !e["rows"].is_array()) continue;
        UndoSet s;
        s.pack = e["pack"].get<std::string>();
        if (e.contains("season") && e["season"].is_string()) s.season = e["season"].get<std::string>();
        if (e.contains("when") && e["when"].is_string()) s.when = e["when"].get<std::string>();
        for (const auto& r : e["rows"]) {
            if (!r.is_object() || !r.contains("id") || !r["id"].is_number_integer() || !r.contains("from") || !r["from"].is_number_integer() ||
                !r.contains("to") || !r["to"].is_number_integer())
                continue;
            UndoRow u;
            u.kind = r.value("kind", std::string("player")) == "manager" ? Kind::Manager : Kind::Transfer;
            u.id = r["id"].get<int64_t>();
            u.from = r["from"].get<int64_t>();
            u.to = r["to"].get<int64_t>();
            u.displaced = r.contains("displaced") && r["displaced"].is_number_integer() ? r["displaced"].get<int64_t>() : 0;
            u.subject = r.value("subject", std::string());
            s.rows.push_back(u);
        }
        if (!s.rows.empty()) out.sets.push_back(s);
    }
    return true;
}

fs::path undo_store_path(const fs::path& le_root) { return le_root / "turbo_output" / "season_pack_undo.json"; }

bool load_undo_store(const fs::path& p, UndoStore& out, std::string* err) {
    out = UndoStore{};
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::string text;
    if (!read_text(p, text)) {
        if (err) *err = "cannot read " + p.string();
        return false;
    }
    return parse_undo_json(text, out, err);
}

bool save_undo_store(const fs::path& p, const UndoStore& s, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const std::string text = undo_json(s);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {   // Windows: a rename over a file another program holds open can fail; replace in two steps
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.string();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- undo
UndoPlan make_undo(const UndoSet& set, const Snapshot& live) {
    UndoPlan plan;
    Snapshot w = live;
    std::map<int64_t, int> squad = w.squad_sizes();
    struct Step {
        Kind kind;
        int64_t id, team, replacement;
    };
    std::vector<Step> steps;
    auto name_of = [&](const UndoRow& r) { return !r.subject.empty() ? r.subject : std::string(r.kind == Kind::Manager ? "manager " : "player ") + std::to_string(r.id); };
    for (size_t k = set.rows.size(); k-- > 0;) {
        const UndoRow& r = set.rows[k];
        const std::string nm = name_of(r);
        if (r.kind == Kind::Transfer) {
            auto it = w.player_club.find(r.id);
            if (it == w.player_club.end()) { plan.skipped.push_back(nm + ": not in the career any more"); continue; }
            const int64_t cur = it->second == 0 ? kFreeAgents : it->second;
            if (cur != r.to) { plan.skipped.push_back(nm + ": moved since (now at " + w.team_label(cur) + ")"); continue; }
            if (w.on_loan.count(r.id)) { plan.skipped.push_back(nm + ": on loan now"); continue; }
            if (r.from != kFreeAgents) {
                auto tt = w.teams.find(r.from);
                if (tt == w.teams.end() || tt->second.national) { plan.skipped.push_back(nm + ": " + w.team_label(r.from) + " is not a club of this career"); continue; }
            }
            if (at_user(w, r.from) || at_user(w, r.to)) { plan.skipped.push_back(nm + ": touches your club"); continue; }
            if (r.to != kFreeAgents && squad[r.to] >= kMinSquad && squad[r.to] - 1 < kMinSquad) {
                plan.skipped.push_back(nm + ": " + w.team_label(r.to) + " would drop below " + std::to_string(kMinSquad) + " players");
                continue;
            }
            if (r.from != kFreeAgents && squad[r.from] >= kMaxSquad) { plan.skipped.push_back(nm + ": " + w.team_label(r.from) + " is full"); continue; }
            if (r.to != kFreeAgents) --squad[r.to];
            if (r.from != kFreeAgents) ++squad[r.from];
            w.player_club[r.id] = r.from;
            steps.push_back({Kind::Transfer, r.id, r.from, 0});
            plan.done.push_back(nm + ": " + w.team_label(r.to) + " -> " + w.team_label(r.from));
        } else {
            auto it = w.manager_team.find(r.id);
            if (it == w.manager_team.end()) { plan.skipped.push_back(nm + ": not in the career any more"); continue; }
            if (it->second != r.to) { plan.skipped.push_back(nm + ": moved since (now at " + w.team_label(it->second) + ")"); continue; }
            if (r.displaced != 0) {
                auto dt = w.manager_team.find(r.displaced);
                if (dt == w.manager_team.end() || dt->second != r.from) { plan.skipped.push_back(nm + ": his swap partner moved since"); continue; }
            }
            if (at_user(w, r.from) || at_user(w, r.to)) { plan.skipped.push_back(nm + ": touches your club"); continue; }
            if (r.from <= 0 && r.displaced == 0) { plan.skipped.push_back(nm + ": he came from the free agents and nobody took his place to give back"); continue; }
            w.manager_team[r.id] = r.from;
            if (r.displaced != 0) w.manager_team[r.displaced] = r.to;
            steps.push_back({Kind::Manager, r.id, r.from > 0 ? r.from : 0, r.from > 0 ? 0 : r.displaced});
            plan.done.push_back(nm + ": " + w.team_label(r.to) + " -> " + w.team_label(r.from));
        }
    }
    for (size_t a = 0; a < steps.size(); a += kPageRows) {
        Page pg;
        const size_t end = std::min(steps.size(), a + kPageRows);
        std::vector<json> actions;
        for (size_t i = a; i < end; ++i)
            if (steps[i].kind == Kind::Transfer) actions.push_back(transfer_action(steps[i].id, steps[i].team));
        push_player_sends(pg.sends, actions);
        for (size_t i = a; i < end; ++i)
            if (steps[i].kind == Kind::Manager) pg.sends.push_back(manager_send(steps[i].id, steps[i].team, steps[i].replacement));
        pg.rows.resize(end - a);
        plan.pages.push_back(std::move(pg));
    }
    return plan;
}

}  // namespace season_pack
}  // namespace turbo
