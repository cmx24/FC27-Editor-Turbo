// FC 27 LE Turbo GUI - bench probe (see bench_probe.h). Read-only.
#include "bench_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// ------------------------------------------------------------------------------------------------ bench keep (2.15)
std::string shuffle_of(const Ids& saved, const Ids& now) {
    if (saved == now) return "same order";
    for (int k = 0; k < kStarters; ++k)
        if (saved[size_t(k)] != now[size_t(k)])
            return "the starting XI changed (#" + std::to_string(k) + " " + std::to_string(saved[size_t(k)]) + " -> " + std::to_string(now[size_t(k)]) + ")";
    for (int k = kStarters; k < kIds; ++k)
        if ((saved[size_t(k)] == -1) != (now[size_t(k)] == -1)) return "the empty places moved (#" + std::to_string(k) + ")";
    std::vector<int32_t> a(saved.begin() + kStarters, saved.end()), b(now.begin() + kStarters, now.end());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) {
        std::string s = "other players on the sheet";
        const std::vector<int32_t> fa = filled(saved), fb = filled(now);
        std::set<int32_t> sa(fa.begin(), fa.end()), sb(fb.begin(), fb.end());
        std::string on, off;
        for (int32_t pid : fb)
            if (!sa.count(pid)) on += (on.empty() ? "" : ", ") + std::to_string(pid);
        for (int32_t pid : fa)
            if (!sb.count(pid)) off += (off.empty() ? "" : ", ") + std::to_string(pid);
        if (!on.empty()) s += " (on now: " + on + ")";
        if (!off.empty()) s += " (off now: " + off + ")";
        return s;
    }
    for (const Ids* ids : {&saved, &now}) {
        std::set<int32_t> seen;
        for (int32_t pid : *ids)
            if (pid != -1 && !seen.insert(pid).second) return "player " + std::to_string(pid) + " is on the sheet twice";
    }
    return "";
}

int moved_places(const Ids& a, const Ids& b) {
    int n = 0;
    for (int k = kStarters; k < kIds; ++k)
        if (a[size_t(k)] != b[size_t(k)]) ++n;
    return n;
}

std::vector<std::string> Probe::on_copies(const Ids& map, int map_dirty, const std::vector<CopyView>& copies, const Namer& name) {
    std::vector<std::string> out;
    // your map sheet: a commit changes its ids, a save clears its dirty flag (Lua's save signal is blind on LE 27.1.5)
    if (have_map_ && (map != map_ || map_dirty != map_dirty_))
        out.push_back("bench probe: your map sheet changed [dirty " + std::to_string(map_dirty_) + " -> " + std::to_string(map_dirty) + "]: " +
                      diff_text(map_, map, name) + " | " + order_text(map, name));
    have_map_ = true;
    map_ = map;
    map_dirty_ = map_dirty;
    if (copies_.size() != copies.size()) copies_.assign(copies.size(), CopyView());
    for (size_t i = 0; i < copies.size(); ++i) {
        const CopyView& c = copies[i];
        CopyView& was = copies_[i];
        if (c.live && (!was.live || c.build != was.build)) {
            char b[32];
            std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(c.build));
            out.push_back("bench probe: Team Management's working copy " + c.label + " built [records " + b + "]: against your map sheet (your saved order): " +
                          diff_text(map, c.ids, name) + " | " + order_text(c.ids, name));
        } else if (c.live && was.live && c.ids != was.ids) {
            out.push_back("bench probe: Team Management's working copy " + c.label + " changed: " + diff_text(was.ids, c.ids, name) +
                          " | against your map sheet: " + diff_text(map, c.ids, name));
        } else if (!c.live && was.live) {
            out.push_back("bench probe: Team Management's working copy " + c.label + " is no longer your sheet's (Team Management closed?)");
        }
        was = c;
    }
    return out;
}

KeepOut Keeper::on_read(long long load_gen, const Ids& map, int map_dirty, const CopyView& copy, bool on, const std::string& off_why, bool may_write,
                        const std::string& wait_in, double now, const Namer& name) {
    KeepOut o;
    off_why_ = off_why;
    // your map sheet's last change (ids or dirty flag): the write waits kMapQuietS after it (a commit or a save may be running)
    if (map_dirty_seen_ == -2 || map != map_seen_ || map_dirty != map_dirty_seen_) {
        map_seen_ = map;
        map_dirty_seen_ = map_dirty;
        map_changed_at_ = now;
    }
    std::string wait = wait_in;
    if (may_write && now - map_changed_at_ < kMapQuietS) {
        may_write = false;
        wait = "your map sheet changed less than " + seconds(kMapQuietS) + " ago (a commit or a save may be running)";
    }
    if (load_gen != gen_) {
        if (ph_ == KeepPhase::Pending) o.log.push_back("bench keep: the career was (re)loaded: the write is dropped (nothing written)");
        gen_ = load_gen;
        primed_ = false;
        if (ph_ != KeepPhase::Off && ph_ != KeepPhase::Queued) ph_ = KeepPhase::Idle;
    }
    if (ph_ == KeepPhase::Queued && now > queued_at_ + kKeepJobLostS) {
        outcome_ = "not written: the game thread did not run the job";
        o.log.push_back("bench keep: " + outcome_ + " (nothing written by it)");
        ph_ = KeepPhase::Done;
    }
    if (!on) {
        if (ph_ == KeepPhase::Pending)
            o.log.push_back("bench keep: switched off (" + (off_why.empty() ? std::string("no turbo_output\\") + keep_on_file() : off_why) +
                            "): the write is dropped (nothing written)");
        if (ph_ != KeepPhase::Queued) ph_ = KeepPhase::Off;  // a queued job re-reads the switches itself
        primed_ = false;  // switched on later: the copy's state is learnt first
        return o;
    }
    if (ph_ == KeepPhase::Off) ph_ = KeepPhase::Idle;
    if (!primed_) {
        primed_ = true;
        was_live_ = copy.live;
        build_ = copy.live ? copy.build : 0;
        o.log.push_back(std::string("bench keep: watching Team Management's working copy ") + copy.label +
                        (copy.live ? " (built already: left alone, the next build is written)" : " (not built: Team Management closed)"));
        return o;
    }
    if (!copy.live) {
        if (was_live_ && ph_ == KeepPhase::Pending) {
            outcome_ = "not written: the working copy went away before the write (Team Management closed?)";
            o.log.push_back("bench keep: " + outcome_);
            ph_ = KeepPhase::Done;
        }
        was_live_ = false;
        return o;
    }
    if (!was_live_ || copy.build != build_) {  // a new build
        was_live_ = true;
        build_ = copy.build;
        built_at_ = now;
        last_refusal_.clear();
        last_wait_.clear();
        const std::string w = shuffle_of(map, copy.ids);
        if (w.empty()) {
            saved_ = map;
            expect_ = copy.ids;
            moved_ = moved_places(map, copy.ids);
            next_try_ = 0.0;
            ph_ = KeepPhase::Pending;
            o.log.push_back("bench keep: Team Management built its working copy " + copy.label + " in another order than your saved one (" +
                            std::to_string(moved_) + " places of the bench / reserves; the starting XI and the players unchanged): " +
                            diff_text(map, copy.ids, name) + ". Writing your saved order into it");
        } else if (w == "same order") {
            outcome_ = "Team Management's working copy holds your saved order: nothing to write";
            o.log.push_back("bench keep: " + outcome_);
            ph_ = KeepPhase::Done;
        } else {
            outcome_ = "left alone: Team Management's working copy is not your saved players in another order (" + w + ")";
            o.log.push_back("bench keep: " + outcome_);
            ph_ = KeepPhase::Done;
        }
    }
    if (ph_ != KeepPhase::Pending) return o;
    if (copy.ids != expect_) {
        outcome_ = "left alone: the working copy changed before the write (a move on the screen?)";
        o.log.push_back("bench keep: " + outcome_ + " (nothing written)");
        ph_ = KeepPhase::Done;
        return o;
    }
    if (map != saved_) {
        outcome_ = "left alone: your saved order (the map) changed before the write";
        o.log.push_back("bench keep: " + outcome_ + " (nothing written)");
        ph_ = KeepPhase::Done;
        return o;
    }
    if (now > built_at_ + kKeepDeadlineS) {
        outcome_ = "not written: " + seconds(kKeepDeadlineS) + " after the build the write was still waiting (" +
                   (!may_write ? wait : !last_refusal_.empty() ? last_refusal_ : std::string("not queued")) + ")";
        o.log.push_back("bench keep: " + outcome_ + ": given up (nothing written)");
        ph_ = KeepPhase::Done;
        return o;
    }
    if (!may_write) {
        if (wait != last_wait_) {
            last_wait_ = wait;
            o.log.push_back("bench keep: the write waits (" + wait + "): until " + seconds(kKeepDeadlineS) + " after the build");
        }
        return o;
    }
    if (now >= next_try_) {
        next_try_ = now + kKeepRetryS;
        o.submit = true;
        o.saved = saved_;
        o.before = expect_;
        o.build = build_;
    }
    return o;
}

void Keeper::queued(double now) {
    ph_ = KeepPhase::Queued;
    queued_at_ = now;
}

void Keeper::not_queued(const std::string& why, std::vector<std::string>& log) {
    if (why != last_refusal_) {
        last_refusal_ = why;
        log.push_back("bench keep: the write is not queued yet (" + why + "): tried again until " + seconds(kKeepDeadlineS) + " after the build");
    }
}

void Keeper::on_result(bool written, const std::string& line) {
    outcome_ = (written ? "put back: " : "not written: ") + line;
    if (ph_ == KeepPhase::Queued) ph_ = KeepPhase::Done;
}

std::string Keeper::state() const {
    const std::string last = outcome_.empty() ? std::string() : " (last: " + outcome_ + ")";
    switch (ph_) {
        case KeepPhase::Off:
            return off_why_.empty() ? std::string("off (no turbo_output\\") + keep_on_file() + ")" : "off: " + off_why_;
        case KeepPhase::Idle: return "on: waiting for Team Management to build its copy of your sheet" + last;
        case KeepPhase::Pending:
            return "on: Team Management's copy has another order (" + std::to_string(moved_) + " places): writing your saved order into it";
        case KeepPhase::Queued: return "on: writing your saved order into Team Management's copy ...";
        case KeepPhase::Done: return "on: " + outcome_;
    }
    return "";
}

}  // namespace bench
}  // namespace turbo
