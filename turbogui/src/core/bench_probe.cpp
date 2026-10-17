// FC 27 LE Turbo GUI - bench probe (see bench_probe.h). Read-only.
#include "bench_probe.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace turbo {
namespace bench {

namespace {

std::string seconds(double s) {
    const long long n = std::llround(s < 0 ? 0 : s);
    return std::to_string(n) + " s";
}

std::string who(const Namer& name, int32_t pid) { return name ? name(pid) : std::to_string(pid); }

int last_filled(const Ids& ids) {
    int last = -1;
    for (int k = 0; k < kIds; ++k)
        if (ids[size_t(k)] != -1) last = k;
    return last;
}

std::string id_list(const std::vector<int32_t>& v, const Namer& name) {
    std::string s;
    for (int32_t pid : v) s += (s.empty() ? "" : ", ") + who(name, pid);
    return s;
}

}  // namespace

std::vector<int32_t> filled(const Ids& ids) {
    std::vector<int32_t> out;
    for (int32_t pid : ids)
        if (pid != -1) out.push_back(pid);
    return out;
}

std::string order_text(const Ids& ids, const Namer& name) {
    const int last = last_filled(ids);
    std::string s = std::to_string(filled(ids).size()) + " players:";
    for (int k = 0; k <= last; ++k) {
        const int32_t pid = ids[size_t(k)];
        s += (k ? ", #" : " #") + std::to_string(k) + " " + (pid == -1 ? std::string("(empty)") : who(name, pid));
    }
    return s;
}

std::string diff_text(const Ids& before, const Ids& after, const Namer& name) {
    if (before == after) return "same order";
    std::vector<int32_t> a = filled(before), b = filled(after);
    std::set<int32_t> sa(a.begin(), a.end()), sb(b.begin(), b.end());
    std::vector<int32_t> joined, left;
    for (int32_t pid : b)
        if (!sa.count(pid)) joined.push_back(pid);
    for (int32_t pid : a)
        if (!sb.count(pid)) left.push_back(pid);
    // moves: a player on both lists whose index changed
    std::string moves;
    int moved = 0;
    for (int k = 0; k < kIds; ++k) {
        const int32_t pid = before[size_t(k)];
        if (pid == -1 || !sb.count(pid)) continue;
        int to = -1;
        for (int j = 0; j < kIds; ++j)
            if (after[size_t(j)] == pid) {
                to = j;
                break;
            }
        if (to == k) continue;
        if (++moved <= kMaxMovesListed) moves += (moves.empty() ? "" : ", ") + who(name, pid) + " #" + std::to_string(k) + "->#" + std::to_string(to);
    }
    const size_t places = std::max(a.size(), b.size());
    std::string s;
    if (moved == 0 && joined.empty() && left.empty())
        s = "the same players at the same places (empty slots moved)";
    else if (moved == 0)
        s = "nobody else moved";
    else
        s = std::to_string(moved) + " of " + std::to_string(places) + " places changed: " + moves + (moved > kMaxMovesListed ? ", ..." : "");
    if (!joined.empty()) s += "; on the sheet now: " + id_list(joined, name);
    if (!left.empty()) s += "; off the sheet now: " + id_list(left, name);
    return s;
}

std::vector<int32_t> missing(const std::vector<int32_t>& squad, const Ids& ids) {
    std::set<int32_t> on;
    for (int32_t pid : ids)
        if (pid != -1) on.insert(pid);
    std::vector<int32_t> out;
    for (int32_t pid : squad)
        if (!on.count(pid) && std::find(out.begin(), out.end(), pid) == out.end()) out.push_back(pid);
    return out;
}

std::vector<int32_t> extra(const std::vector<int32_t>& squad, const Ids& ids) {
    std::set<int32_t> sq(squad.begin(), squad.end());
    std::vector<int32_t> out;
    for (int32_t pid : ids)
        if (pid != -1 && !sq.count(pid)) out.push_back(pid);
    return out;
}

Due Probe::step(const Input& in) {
    Due d;
    now_ = in.now;
    if (!in.career) {
        tm_ = false;
        close_until_ = -1.0;
        return d;
    }
    if (in.load_gen != gen_) {  // a new career load: start over (the sheet came from the database)
        *this = Probe();
        gen_ = in.load_gen;
        save_t_ = in.save_t;
        now_ = in.now;
    }
    if (in.team_mgmt && !tm_) {
        tm_ = true;
        opened_at_ = in.now;
        next_poll_ = in.now + kPollS;
        close_until_ = -1.0;
        return {Kind::Opened, "Team Management opened", true};
    }
    if (!in.team_mgmt && tm_) {
        tm_ = false;
        closed_at_ = in.now;
        next_poll_ = in.now + kPollS;
        close_until_ = in.now + kAfterCloseS;
        return {Kind::Closed, "Team Management closed (open " + seconds(in.now - opened_at_) + ")", true};
    }
    if (in.save_t > save_t_) {
        save_t_ = in.save_t;
        return {Kind::Save, std::string("save started") + (tm_ ? " (Team Management open)" : ""), true};
    }
    if (tm_) {
        if (in.now >= next_poll_) {
            next_poll_ = in.now + kPollS;
            return {Kind::Open, seconds(in.now - opened_at_) + " after Team Management opened", false};
        }
        return d;
    }
    if (close_until_ > 0) {
        if (in.now >= close_until_) {
            close_until_ = -1.0;
            return {Kind::Final, seconds(in.now - closed_at_) + " after Team Management closed", true};
        }
        if (in.now >= next_poll_) {
            next_poll_ = in.now + kPollS;
            return {Kind::After, seconds(in.now - closed_at_) + " after Team Management closed", false};
        }
        return d;
    }
    if (!load_done_ && in.phase == "hub" && in.now >= load_retry_) {
        load_done_ = true;
        return {Kind::Load, "career hub (the sheet as loaded)", true};
    }
    return d;
}

std::vector<std::string> Probe::on_read(const Due& d, const Ids& ids, const std::vector<int32_t>& squad, const Namer& name,
                                        const std::string& meta) {
    std::vector<std::string> out;
    const bool changed = have_last_ && last_ != ids;
    if (d.always || changed) {
        std::string s = "bench probe: " + d.label + (meta.empty() ? "" : " [" + meta + "]") + ": ";
        if (have_last_)
            s += (changed ? "CHANGED since the last read: " + diff_text(last_, ids, name) : std::string("same order as the last read")) + " | ";
        s += order_text(ids, name);
        out.push_back(s);
        if (d.always && !squad.empty()) {  // an empty squad: the model is not loaded (nothing to compare)
            const std::vector<int32_t> miss = missing(squad, ids), more = extra(squad, ids);
            if (!miss.empty()) out.push_back("bench probe: " + std::to_string(miss.size()) + " squad player(s) not on the sheet: " + id_list(miss, name));
            if (!more.empty()) out.push_back("bench probe: " + std::to_string(more.size()) + " sheet player(s) not in the squad: " + id_list(more, name));
        }
    }
    if (d.kind == Kind::Opened) {
        open_ = ids;
        have_open_ = true;
        have_close_ = false;
    } else if (d.kind == Kind::Closed) {
        close_ = ids;
        have_close_ = true;
    } else if (d.kind == Kind::Final) {
        std::string s = "bench probe: summary:";
        if (have_open_ && have_close_) s += " opened -> closed: " + diff_text(open_, close_, name) + ";";
        if (have_close_) s += " closed -> " + d.label + ": " + diff_text(close_, ids, name);
        if (have_open_) s += "; opened -> now: " + diff_text(open_, ids, name);
        out.push_back(s);
    }
    last_ = ids;
    have_last_ = true;
    return out;
}

std::string Probe::on_fail(const Due& d, const std::string& why) {
    if (d.kind == Kind::Load) {  // the career load's read is tried again (the service may still be locating)
        load_done_ = false;
        load_retry_ = now_ + kRetryS;
    }
    if (++fails_ > kMaxFailLogs) return "";
    return "bench probe: " + d.label + ": not read: " + why + (fails_ == kMaxFailLogs ? " (further failures in this career load are not logged)" : "");
}

}  // namespace bench
}  // namespace turbo
