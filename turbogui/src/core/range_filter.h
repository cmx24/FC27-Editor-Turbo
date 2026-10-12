// FC 27 LE Turbo GUI - two-sided integer filters ("age 20 to 30"). Platform independent, header only.
// Each side is 0 = no bound, so a filter that used to be one-sided ("Max age 25") is from 0, to 25. Used by the
// Players list filters (ui_players.cpp) through widgets.cpp range_filter_row().
#pragma once
#include <algorithm>
#include <string>

namespace turbo {

struct IntRange {
    int from = 0;  // 0 = no lower bound
    int to = 0;    // 0 = no upper bound
    bool active() const { return from > 0 || to > 0; }
    void clear() { from = to = 0; }
};

// Keeps both sides inside [0, hi] and from <= to when both are set. The side the user just edited wins: with
// `to_changed` the "from" box follows a lower "to", else the "to" box follows a higher "from".
inline void range_normalize(int& from, int& to, int hi, bool to_changed = false) {
    from = std::max(0, std::min(from, hi));
    to = std::max(0, std::min(to, hi));
    if (from > 0 && to > 0 && from > to) {
        if (to_changed) from = to;
        else to = from;
    }
}
inline void range_normalize(IntRange& r, int hi, bool to_changed = false) { range_normalize(r.from, r.to, hi, to_changed); }

// Does `v` pass the range? A value that is not known (`known` false: an age without a birth date) fails any set bound
// and passes an unbounded range, as the old one-sided filters did.
inline bool range_passes(int from, int to, int v, bool known = true) {
    if (from <= 0 && to <= 0) return true;
    if (!known) return false;
    if (from > 0 && v < from) return false;
    if (to > 0 && v > to) return false;
    return true;
}
inline bool range_passes(const IntRange& r, int v, bool known = true) { return range_passes(r.from, r.to, v, known); }

// "Age 20-30", "Age from 20", "Age up to 30", "" when unbounded
inline std::string range_text(const char* what, int from, int to) {
    if (from <= 0 && to <= 0) return "";
    std::string s = what;
    if (from > 0 && to > 0) return s + " " + std::to_string(from) + "-" + std::to_string(to);
    if (from > 0) return s + " from " + std::to_string(from);
    return s + " up to " + std::to_string(to);
}

}  // namespace turbo
