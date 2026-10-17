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

void Keeper::reset(KeepPhase to) {
    ph_ = to;
    visit_save_t0_ = 0;
    saw_save_ = have_opened_ = have_saved_ = saved_at_save_ = false;
    opened_ = saved_ = expect_ = Ids{};
    closed_at_ = next_try_ = queued_at_ = 0.0;
    last_refusal_.clear();
    moved_ = 0;
}

KeepTick Keeper::step(const Input& in, bool on, const std::string& off_why, long long save_age) {
    KeepTick t;
    now_ = in.now;
    off_why_ = off_why;
    const KeepPhase idle = on ? KeepPhase::Idle : KeepPhase::Off;
    if (!in.career) {
        if (ph_ == KeepPhase::Pending) t.log.push_back("bench keep: the career was left: the write-back is dropped (nothing written)");
        if (ph_ != idle) reset(idle);
        tm_ = false;
        return t;
    }
    if (in.load_gen != gen_) {
        if (ph_ == KeepPhase::Pending) t.log.push_back("bench keep: the career was (re)loaded: the write-back is dropped (nothing written)");
        reset(idle);
        gen_ = in.load_gen;
        tm_ = in.team_mgmt;  // a visit starts only when Team Management opens from here on
        return t;
    }
    if (!on) {
        if (ph_ == KeepPhase::Pending)
            t.log.push_back("bench keep: switched off (" + (off_why.empty() ? std::string("no turbo_output\\") + keep_on_file() : off_why) +
                            "): the write-back is dropped (nothing written)");
        if (ph_ == KeepPhase::Queued && in.now > queued_at_ + kKeepJobLostS) ph_ = KeepPhase::Done;
        if (ph_ != KeepPhase::Queued && ph_ != KeepPhase::Off) reset(KeepPhase::Off);  // a queued job re-reads the switches itself
        tm_ = in.team_mgmt;
        return t;
    }
    if (ph_ == KeepPhase::Off) ph_ = KeepPhase::Idle;
    if (in.team_mgmt && !tm_) {
        tm_ = true;
        if (ph_ == KeepPhase::Pending)
            t.log.push_back("bench keep: Team Management opened again before the write-back: dropped (Turbo never writes while it is open)");
        if (ph_ != KeepPhase::Queued) {  // a queued job refuses in the job (G2: Team Management open)
            reset(KeepPhase::Open);
            visit_save_t0_ = in.save_t;
        }
        return t;
    }
    if (!in.team_mgmt && tm_) {
        tm_ = false;
        if (ph_ == KeepPhase::Open) {
            ph_ = KeepPhase::Watch;
            closed_at_ = in.now;
            if (in.save_t > visit_save_t0_) saw_save_ = true;
        }
        return t;
    }
    switch (ph_) {
        case KeepPhase::Watch:
            // a save seen together with the close (one bridge poll) or right after it counts for this visit
            if (in.save_t > visit_save_t0_ && in.now <= closed_at_ + kSaveSlackS) saw_save_ = true;
            if (in.now > closed_at_ + kAfterCloseS + 5.0) {  // the probe's last read did not come (failed reads)
                t.log.push_back("bench keep: no reorder seen after Team Management closed: nothing to do");
                ph_ = KeepPhase::Idle;
            }
            break;
        case KeepPhase::Pending: {
            std::string wait;
            if (in.phase != "hub") wait = "Lua's phase is " + (in.phase.empty() ? std::string("not reported") : in.phase) + ", not the hub";
            else if (save_age >= 0 && save_age < kKeepSaveQuietS) wait = "a save started " + std::to_string(save_age) + " s ago";
            if (in.now > closed_at_ + kKeepDeadlineS) {
                outcome_ = "not written back: " + std::to_string(int(kKeepDeadlineS)) + " s after Team Management closed the write-back was still waiting (" +
                           (!wait.empty() ? wait : !last_refusal_.empty() ? last_refusal_ : std::string("not queued")) + ")";
                t.log.push_back("bench keep: " + outcome_ + ": given up (nothing written)");
                ph_ = KeepPhase::Done;
            } else if (wait.empty() && in.now >= next_try_) {
                next_try_ = in.now + kKeepRetryS;
                t.submit = true;
            }
            break;
        }
        case KeepPhase::Queued:
            if (in.now > queued_at_ + kKeepJobLostS) {
                outcome_ = "not written back: the game thread did not run the job";
                t.log.push_back("bench keep: " + outcome_ + " (nothing written by it)");
                ph_ = KeepPhase::Done;
            }
            break;
        default: break;
    }
    return t;
}

std::vector<std::string> Keeper::on_read(const Due& d, const Ids& ids, const Namer& name) {
    std::vector<std::string> out;
    if (ph_ == KeepPhase::Open) {
        if (d.kind == Kind::Opened) {
            opened_ = ids;
            have_opened_ = true;
        } else if (d.kind == Kind::Save) {
            saved_ = ids;
            have_saved_ = saved_at_save_ = saw_save_ = true;
            out.push_back("bench keep: your order at the save (Team Management open) is the one to keep: " + std::to_string(filled(ids).size()) + " players");
        }
        return out;
    }
    if (ph_ != KeepPhase::Watch) return out;
    if (d.kind == Kind::Closed && !saved_at_save_) {
        saved_ = ids;
        have_saved_ = true;
        out.push_back("bench keep: no save seen while Team Management was open: the order at the close is the one to keep: " + std::to_string(filled(ids).size()) +
                      " players");
        return out;
    }
    if (d.kind != Kind::Closed && d.kind != Kind::After && d.kind != Kind::Final && d.kind != Kind::Save) return out;
    if (!have_saved_) return out;
    const std::string w = shuffle_of(saved_, ids);
    const std::string when = seconds(now_ - closed_at_) + " after Team Management closed";
    if (w.empty()) {
        if (!saw_save_ && have_opened_ && ids == opened_) {
            outcome_ = "left alone: the order went back to the one Team Management opened with and no save was seen (a No?)";
            out.push_back("bench keep: " + when + ": " + outcome_);
            ph_ = KeepPhase::Done;
            return out;
        }
        expect_ = ids;
        moved_ = moved_places(saved_, ids);
        ph_ = KeepPhase::Pending;
        next_try_ = 0.0;
        out.push_back("bench keep: the game reordered your bench / reserves " + when + " (" + std::to_string(moved_) + " places; the starting XI and the players " +
                      "unchanged): " + diff_text(saved_, ids, name) + ". Your saved order goes back once the career hub is quiet (no save in the last " +
                      std::to_string(kKeepSaveQuietS) + " s)");
        return out;
    }
    if (w == "same order") {
        if (d.kind == Kind::Final) {
            out.push_back("bench keep: your order held for " + seconds(kAfterCloseS) + " after Team Management closed: nothing to do");
            ph_ = KeepPhase::Idle;
        }
        return out;
    }
    outcome_ = "left alone: the sheet changed after Team Management closed, not only its order (" + w + ")";
    out.push_back("bench keep: " + when + ": " + outcome_);
    ph_ = KeepPhase::Done;
    return out;
}

std::string Keeper::prepare(const Ids& now, Ids& saved, Ids& before, std::vector<std::string>& log, const Namer& name) {
    if (ph_ != KeepPhase::Pending) return "no write-back is waiting";
    if (now == saved_) {
        outcome_ = "nothing written: the sheet holds your saved order again (the game put it back itself)";
        log.push_back("bench keep: " + outcome_);
        ph_ = KeepPhase::Done;
        return "already in place";
    }
    const std::string w = shuffle_of(saved_, now);
    if (!w.empty()) {
        outcome_ = "nothing written: before the write-back the sheet changed, not only its order (" + w + ")";
        log.push_back("bench keep: " + outcome_);
        ph_ = KeepPhase::Done;
        return w;
    }
    if (now != expect_) {
        log.push_back("bench keep: the order changed again since the reorder was seen (still your players in another order): " + diff_text(expect_, now, name) +
                      "; the write-back starts from the latest");
        expect_ = now;
        moved_ = moved_places(saved_, now);
    }
    saved = saved_;
    before = now;
    return "";
}

void Keeper::queued(double now) {
    ph_ = KeepPhase::Queued;
    queued_at_ = now;
}

void Keeper::not_queued(const std::string& why, std::vector<std::string>& log) {
    if (why != last_refusal_) {
        last_refusal_ = why;
        log.push_back("bench keep: the write-back is not queued yet (" + why + "): tried again until " + seconds(kKeepDeadlineS) + " after the close");
    }
}

void Keeper::on_result(bool written, const std::string& line) {
    outcome_ = (written ? "put back: " : "not written back: ") + line;
    if (ph_ == KeepPhase::Queued) ph_ = KeepPhase::Done;
}

std::string Keeper::state() const {
    const std::string last = outcome_.empty() ? std::string() : " (last: " + outcome_ + ")";
    switch (ph_) {
        case KeepPhase::Off:
            return off_why_.empty() ? std::string("off (no turbo_output\\") + keep_on_file() + ")" : "off: " + off_why_;
        case KeepPhase::Idle: return "on: waiting for Team Management" + last;
        case KeepPhase::Open: return "on: Team Management is open (your order is taken when the game saves, or when you leave)";
        case KeepPhase::Watch: return "on: watching your order for " + seconds(kAfterCloseS) + " after Team Management closed";
        case KeepPhase::Pending:
            return "on: the game reordered your bench (" + std::to_string(moved_) + " places): your saved order goes back once the hub is quiet";
        case KeepPhase::Queued: return "on: writing your saved order back ...";
        case KeepPhase::Done: return "on: " + outcome_;
    }
    return "";
}

}  // namespace bench
}  // namespace turbo
