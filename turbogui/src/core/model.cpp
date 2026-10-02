#include "model.h"

#include <algorithm>
#include <cmath>

namespace turbo {

// ---------------------------------------------------------------- dates
// players.birthdate / playerjointeamdate hold Lilian day numbers (day 1 = 1582-10-15): Julian day
// number - 2299160. gregorian_days_from_date is Live Editor's DATE:ToGregorianDays; the inverse is
// the exact integer algorithm (the FIFA/FC community birthdate converter uses the same pair).
// Live Editor's own DATE:FromGregorianDays is not used: it returns the following day for many dates.
static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

GameDate date_from_gregorian_days(int64_t days) {
    int64_t a = days + 2331204;  // Julian day number + 32044
    int64_t b = floor_div(4 * a + 3, 146097);
    int64_t c = a - floor_div(146097 * b, 4);
    int64_t d = floor_div(4 * c + 3, 1461);
    int64_t e = c - floor_div(1461 * d, 4);
    int64_t m = floor_div(5 * e + 2, 153);
    GameDate out;
    out.day = static_cast<int>(e - floor_div(153 * m + 2, 5) + 1);
    out.month = static_cast<int>(m + 3 - 12 * floor_div(m, 10));
    out.year = static_cast<int>(100 * b + d - 4800 + floor_div(m, 10));
    return out;
}

int64_t gregorian_days_from_date(const GameDate& dt) {
    int64_t a = floor_div(14 - dt.month, 12);
    int64_t m = dt.month + 12 * a - 3;
    int64_t y = dt.year + 4800 - a;
    return dt.day + floor_div(153 * m + 2, 5) + y * 365 + floor_div(y, 4) - floor_div(y, 100) + floor_div(y, 400) - 2331205;
}

bool is_real_date(const GameDate& d) {
    if (!d.valid()) return false;
    GameDate back = date_from_gregorian_days(gregorian_days_from_date(d));
    return back.year == d.year && back.month == d.month && back.day == d.day;
}

int age_on(const GameDate& birth, const GameDate& today) {
    if (!birth.valid() || !today.valid()) return -1;
    int age = today.year - birth.year;
    if (today.month < birth.month || (today.month == birth.month && today.day < birth.day)) --age;
    return age;
}

static const char* kPositions[] = {"GK",  "SW",  "RWB", "RB",  "RCB", "CB", "LCB", "LB", "LWB", "RDM",
                                   "CDM", "LDM", "RM",  "RCM", "CM",  "LCM", "LM", "RAM", "CAM", "LAM",
                                   "RF",  "CF",  "LF",  "RW",  "RS",  "ST",  "LS", "LW"};

const char* position_name(int pos) {
    if (pos < 0 || pos >= static_cast<int>(sizeof(kPositions) / sizeof(kPositions[0]))) return "?";
    return kPositions[pos];
}

int position_count() { return static_cast<int>(sizeof(kPositions) / sizeof(kPositions[0])); }

// ---------------------------------------------------------------- model
static constexpr int64_t kInternationalLeague = 78;  // FIFA/FC database league id for national teams

void Model::build_names() {
    name_by_nameid_.clear();
    edited_names_.clear();
    std::vector<std::string> sources;
    for (const char* tname : {"playernames", "dcplayernames"}) {
        const Table* t = db_.table(tname);
        if (!t || !t->has("nameid") || !t->has("name")) continue;
        Snapshot s;
        if (!s.load(db_.memory(), *t)) continue;
        const Field* fid = t->field("nameid");
        const Field* fname = t->field("name");
        for (uint32_t i : s.valid) {
            int64_t id = s.get_int(i, *fid);
            if (!name_by_nameid_.count(id)) name_by_nameid_[id] = s.get_str(i, *fname);
        }
        sources.push_back(tname);
    }
    if (const Table* t = db_.table("editedplayernames")) {
        if (t->has("playerid")) {
            Snapshot s;
            if (s.load(db_.memory(), *t)) {
                for (uint32_t i : s.valid) {
                    int64_t pid = s.get_int(i, "playerid");
                    std::string common = t->has("commonname") ? s.get_str(i, *t->field("commonname")) : "";
                    std::string first = t->has("firstname") ? s.get_str(i, *t->field("firstname")) : "";
                    std::string sur = t->has("surname") ? s.get_str(i, *t->field("surname")) : "";
                    std::string n = !common.empty() ? common : (first.empty() ? sur : (sur.empty() ? first : first + " " + sur));
                    if (!n.empty()) edited_names_[pid] = n;
                }
                sources.push_back("editedplayernames");
            }
        }
    }
    name_source_.clear();
    for (size_t i = 0; i < sources.size(); ++i) name_source_ += (i ? ", " : "") + sources[i];
    if (name_source_.empty()) name_source_ = "none (players shown by ID)";
}

void Model::build_teams() {
    teams_.clear();
    team_index_.clear();
    team_league_.clear();
    if (const Table* l = db_.table("leagueteamlinks")) {
        if (l->has("leagueid") && l->has("teamid")) {
            Snapshot s;
            if (s.load(db_.memory(), *l)) {
                for (uint32_t i : s.valid) team_league_[s.get_int(i, "teamid")] = s.get_int(i, "leagueid");
            }
        }
    }
    const Table* t = db_.table("teams");
    if (!t || !t->has("teamid")) return;
    Snapshot s;
    if (!s.load(db_.memory(), *t)) return;
    const Field* fname = t->field("teamname");
    for (uint32_t i : s.valid) {
        TeamRow r;
        r.teamid = s.get_int(i, "teamid");
        r.rec = s.addr(i);
        r.name = fname ? s.get_str(i, *fname) : "";
        if (r.name.empty()) r.name = "Team " + std::to_string(r.teamid);
        r.overall = static_cast<int>(s.get_int(i, "overallrating", 0));
        auto lg = team_league_.find(r.teamid);
        r.league = lg == team_league_.end() ? -1 : lg->second;
        team_index_[r.teamid] = teams_.size();
        teams_.push_back(r);
    }
    std::sort(teams_.begin(), teams_.end(), [](const TeamRow& a, const TeamRow& b) { return a.name < b.name; });
    team_index_.clear();
    for (size_t k = 0; k < teams_.size(); ++k) team_index_[teams_[k].teamid] = k;
}

void Model::build_links() {
    links_.clear();
    const Table* t = db_.table("teamplayerlinks");
    if (!t || !t->has("teamid") || !t->has("playerid")) return;
    Snapshot s;
    if (!s.load(db_.memory(), *t)) return;
    for (uint32_t i : s.valid) {
        LinkRow r;
        r.rec = s.addr(i);
        r.teamid = s.get_int(i, "teamid");
        r.playerid = s.get_int(i, "playerid");
        r.jersey = static_cast<int>(s.get_int(i, "jerseynumber", -1));
        r.position = static_cast<int>(s.get_int(i, "position", -1));
        links_.push_back(r);
    }
}

bool Model::is_national_team(int64_t tid) const {
    auto it = team_league_.find(tid);
    return it != team_league_.end() && it->second == kInternationalLeague;
}

void Model::build_players(const GameDate& today) {
    players_.clear();
    player_index_.clear();
    const Table* t = db_.table("players");
    if (!t || !t->has("playerid")) return;
    Snapshot s;
    if (!s.load(db_.memory(), *t)) return;

    std::unordered_map<int64_t, int64_t> club_of;
    for (const auto& l : links_) {
        auto it = club_of.find(l.playerid);
        if (it == club_of.end()) club_of[l.playerid] = l.teamid;
        else if (is_national_team(it->second) && !is_national_team(l.teamid)) it->second = l.teamid;
    }

    const Field* ffirst = t->field("firstnameid");
    const Field* flast = t->field("lastnameid");
    const Field* fcommon = t->field("commonnameid");
    const Field* fbirth = t->field("birthdate");
    players_.reserve(s.valid.size());
    for (uint32_t i : s.valid) {
        PlayerRow r;
        r.playerid = s.get_int(i, "playerid");
        r.idx = i;
        r.rec = s.addr(i);
        auto en = edited_names_.find(r.playerid);
        if (en != edited_names_.end()) {
            r.name = en->second;
        } else {
            auto nm = [&](const Field* f) -> std::string {
                if (!f) return "";
                auto it = name_by_nameid_.find(s.get_int(i, *f));
                return it == name_by_nameid_.end() ? "" : it->second;
            };
            std::string common = nm(fcommon), first = nm(ffirst), last = nm(flast);
            r.name = !common.empty() ? common : (first.empty() ? last : (last.empty() ? first : first + " " + last));
        }
        if (r.name.empty()) r.name = "#" + std::to_string(r.playerid);
        auto c = club_of.find(r.playerid);
        r.club = c == club_of.end() ? 0 : c->second;
        r.club_name = r.club ? team_name(r.club) : "";
        r.overall = static_cast<int>(s.get_int(i, "overallrating", 0));
        r.potential = static_cast<int>(s.get_int(i, "potential", 0));
        r.position = static_cast<int>(s.get_int(i, "preferredposition1", -1));
        if (fbirth && today.valid()) r.age = age_on(date_from_gregorian_days(s.get_int(i, *fbirth)), today);
        player_index_[r.playerid] = players_.size();
        players_.push_back(r);
    }
}

void Model::build_managers() {
    managers_.clear();
    const Table* t = db_.table("manager");
    if (!t) return;
    Snapshot s;
    if (!s.load(db_.memory(), *t)) return;
    const Field* ffirst = t->field("firstname");
    const Field* fsur = t->field("surname");
    for (uint32_t i : s.valid) {
        ManagerRow r;
        r.rec = s.addr(i);
        r.managerid = s.get_int(i, "managerid", 0);
        r.teamid = s.get_int(i, "teamid", 0);
        std::string first = ffirst ? s.get_str(i, *ffirst) : "";
        std::string sur = fsur ? s.get_str(i, *fsur) : "";
        r.name = first.empty() ? sur : (sur.empty() ? first : first + " " + sur);
        if (r.name.empty()) r.name = "Manager " + std::to_string(r.managerid);
        managers_.push_back(r);
    }
}

bool Model::rebuild(const GameDate& today) {
    built_ = false;
    ++version_;  // row addresses/pointers handed out before this call are no longer valid
    if (!db_.ready()) return false;
    build_names();
    build_teams();
    build_links();
    build_players(today);
    build_managers();
    built_ = true;
    return true;
}

void Model::refresh_player(int64_t pid, const GameDate& today) {
    auto it = player_index_.find(pid);
    const Table* t = db_.table("players");
    if (it == player_index_.end() || !t) return;
    PlayerRow& r = players_[it->second];
    r.overall = static_cast<int>(db_.get_int(*t, r.rec, "overallrating", r.overall));
    r.potential = static_cast<int>(db_.get_int(*t, r.rec, "potential", r.potential));
    r.position = static_cast<int>(db_.get_int(*t, r.rec, "preferredposition1", r.position));
    if (t->has("birthdate") && today.valid())
        r.age = age_on(date_from_gregorian_days(db_.get_int(*t, r.rec, "birthdate", 0)), today);
}

void Model::refresh_team(int64_t tid) {
    auto it = team_index_.find(tid);
    const Table* t = db_.table("teams");
    if (it == team_index_.end() || !t) return;
    TeamRow& r = teams_[it->second];
    r.overall = static_cast<int>(db_.get_int(*t, r.rec, "overallrating", r.overall));
    if (const Field* f = t->field("teamname")) {
        Value v;
        if (db_.get(*t, r.rec, *f, v) && !v.s.empty()) r.name = v.s;
    }
}

std::string Model::player_name(int64_t pid) const {
    const PlayerRow* p = player(pid);
    return p ? p->name : "#" + std::to_string(pid);
}

std::string Model::team_name(int64_t tid) const {
    auto it = team_index_.find(tid);
    if (it == team_index_.end()) return "Team " + std::to_string(tid);
    return teams_[it->second].name;
}

const PlayerRow* Model::player(int64_t pid) const {
    auto it = player_index_.find(pid);
    return it == player_index_.end() ? nullptr : &players_[it->second];
}

const TeamRow* Model::team(int64_t tid) const {
    auto it = team_index_.find(tid);
    return it == team_index_.end() ? nullptr : &teams_[it->second];
}

std::vector<LinkRow> Model::links_of_player(int64_t pid) const {
    std::vector<LinkRow> out;
    for (const auto& l : links_) if (l.playerid == pid) out.push_back(l);
    return out;
}

std::vector<LinkRow> Model::links_of_team(int64_t tid) const {
    std::vector<LinkRow> out;
    for (const auto& l : links_) if (l.teamid == tid) out.push_back(l);
    std::sort(out.begin(), out.end(), [](const LinkRow& a, const LinkRow& b) { return a.jersey < b.jersey; });
    return out;
}

}  // namespace turbo
