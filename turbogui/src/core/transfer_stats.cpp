// FC 27 LE Turbo GUI - transfer columns (see transfer_stats.h)
#include "transfer_stats.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <system_error>

#include "cmtracker.h"  // parse_csv_text
#include "model.h"      // Lilian day numbers

namespace turbo::xfer {

namespace fs = std::filesystem;

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool to_i64(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = v;
    return true;
}

bool is_loan(const Move& m) { return lower(m.kind).rfind("loan", 0) == 0; }

std::vector<Move> parse_export_csv(const std::string& text, int* skipped) {
    std::vector<Move> out;
    int bad = 0;
    const auto rows = parse_csv_text(text);
    if (rows.empty()) {
        if (skipped) *skipped = 0;
        return out;
    }
    std::map<std::string, size_t> col;
    for (size_t i = 0; i < rows[0].size(); ++i) col[lower(rows[0][i])] = i;
    for (const char* need : {"playerid", "teamfromid", "teamtoid"})
        if (!col.count(need)) {
            if (skipped) *skipped = -1;
            return out;
        }
    auto cell = [&](const std::vector<std::string>& r, const char* name) -> std::string {
        auto it = col.find(name);
        return it != col.end() && it->second < r.size() ? r[it->second] : std::string();
    };
    for (size_t i = 1; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.empty() || (r.size() == 1 && r[0].empty())) continue;  // blank line
        Move m;
        m.source = "export";
        int64_t d = 0, fee = 0;
        if (!to_i64(cell(r, "playerid"), m.playerid) || m.playerid <= 0 || !to_i64(cell(r, "teamfromid"), m.from) ||
            !to_i64(cell(r, "teamtoid"), m.to) || m.from <= 0 || m.to <= 0 || m.from == m.to) {
            ++bad;
            continue;
        }
        m.kind = cell(r, "type").empty() ? "transfer" : cell(r, "type");
        if (to_i64(cell(r, "date"), d) && d >= 19000101 && d <= 21991231) m.date = static_cast<int>(d);
        m.player = cell(r, "playername");
        if (to_i64(cell(r, "total_deal_value"), fee) || to_i64(cell(r, "fee"), fee)) m.fee = std::max<int64_t>(0, fee);
        out.push_back(std::move(m));
    }
    if (skipped) *skipped = bad;
    return out;
}

fs::path newest_export(const fs::path& dir) {
    std::error_code ec;
    fs::path best;
    std::string best_name;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.rfind("turbo_transfer_history_", 0) != 0 || n.size() < 5 || lower(n.substr(n.size() - 4)) != ".csv") continue;
        if (!it->is_regular_file(ec)) continue;
        if (best_name.empty() || n > best_name) {
            best_name = n;
            best = it->path();
        }
    }
    return best;
}

std::vector<Move> read_db_transfers(Database& db) {
    std::vector<Move> out;
    const Table* t = db.table("transfers");
    if (!t) return out;
    const Field *pid = t->field("playerid"), *sell = t->field("sellingteamid"), *buy = t->field("buyingteamid");
    if (!pid || !sell || !buy) return out;
    const Field *amt = t->field("transferamount"), *date = t->field("transferdate");
    Snapshot snap;
    if (!snap.load(db.memory(), *t)) return out;
    for (uint32_t idx : snap.valid) {
        Move m;
        m.source = "database";
        m.kind = "transfer";
        m.playerid = snap.get_int(idx, *pid);
        m.from = snap.get_int(idx, *sell);
        m.to = snap.get_int(idx, *buy);
        if (m.playerid <= 0 || m.from <= 0 || m.to <= 0 || m.from == m.to) continue;
        if (amt) m.fee = std::max<int64_t>(0, snap.get_int(idx, *amt));
        if (date) {
            const GameDate g = date_from_gregorian_days(snap.get_int(idx, *date));
            if (g.valid()) m.date = g.as_int();
        }
        out.push_back(std::move(m));
    }
    return out;
}

static int64_t day_number(int yyyymmdd) {
    GameDate d;
    d.year = yyyymmdd / 10000;
    d.month = yyyymmdd / 100 % 100;
    d.day = yyyymmdd % 100;
    return d.valid() ? gregorian_days_from_date(d) : -1;
}

std::vector<Move> merge(std::vector<Move> a, const std::vector<Move>& b) {
    const size_t n = a.size();
    for (const Move& m : b) {
        bool dup = false;
        for (size_t i = 0; i < n && !dup; ++i) {
            const Move& x = a[i];
            if (x.playerid != m.playerid || x.from != m.from || x.to != m.to) continue;
            const int64_t dx = day_number(x.date), dm = day_number(m.date);
            dup = dx < 0 || dm < 0 || std::llabs(dx - dm) <= 3;
        }
        if (!dup) a.push_back(m);
    }
    return a;
}

std::unordered_map<int64_t, TeamStats> per_team(const std::vector<Move>& moves) {
    std::unordered_map<int64_t, TeamStats> out;
    auto club = [](int64_t id) { return id > 0 && id != kFreeAgents; };
    for (const Move& m : moves) {
        const bool loan = is_loan(m);
        if (club(m.to)) {
            TeamStats& s = out[m.to];
            if (loan) {
                ++s.loans_in;
            } else {
                ++s.in;
                s.spent += m.fee;
            }
        }
        if (club(m.from)) {
            TeamStats& s = out[m.from];
            if (loan) {
                ++s.loans_out;
            } else {
                ++s.out;
                s.received += m.fee;
            }
        }
    }
    return out;
}

std::vector<Move> of_team(const std::vector<Move>& moves, int64_t teamid) {
    std::vector<Move> out;
    for (const Move& m : moves)
        if (m.from == teamid || m.to == teamid) out.push_back(m);
    std::stable_sort(out.begin(), out.end(), [](const Move& x, const Move& y) { return x.date > y.date; });
    return out;
}

std::string money_short(int64_t v) {
    const char* sign = v < 0 ? "-" : "";
    const double a = v < 0 ? -static_cast<double>(v) : static_cast<double>(v);
    char buf[32];
    if (a >= 1e9) std::snprintf(buf, sizeof(buf), "%s%.1fB", sign, a / 1e9);
    else if (a >= 1e6) std::snprintf(buf, sizeof(buf), "%s%.1fM", sign, a / 1e6);
    else if (a >= 1e3) std::snprintf(buf, sizeof(buf), "%s%.0fK", sign, a / 1e3);
    else std::snprintf(buf, sizeof(buf), "%s%.0f", sign, a);
    return buf;
}

std::string date_text(int d) {
    if (d <= 0) return "?";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d / 10000, d / 100 % 100, d % 100);
    return buf;
}

}  // namespace turbo::xfer
