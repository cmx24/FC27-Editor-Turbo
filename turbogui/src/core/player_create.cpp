// FC 27 LE Turbo GUI - "player_create" game call (see player_create.h and docs/re/created_players.md)
#include "player_create.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "bridge.h"
#include "nlohmann/json.hpp"
#include "standings_refresh.h"

namespace turbo {
namespace pc {

using json = nlohmann::json;

const char* action_name(int action) {
    switch (action) {
        case kActionCreate: return "create";
        case kActionCheck: return "check";
        default: return "unknown action";
    }
}

const std::vector<std::string>& name_columns() {
    static const std::vector<std::string> v = {"firstname", "surname", "commonname", "playerjerseyname"};
    return v;
}

const std::vector<std::string>& link_columns() {
    static const std::vector<std::string> v = {"form"};
    return v;
}

const char* Fns::missing() const {
    if (const char* m = move.missing(pm::kActionMove)) return m;
    if (!query_init) return "db_query_init";
    if (!query_set_int) return "db_query_set_int";
    if (!query_set_string) return "db_query_set_string";
    if (!query_select) return "db_query_select_field";
    if (!query_where_int) return "db_query_where_int";
    if (!query_destroy) return "db_query_destroy";
    if (!result_free) return "db_result_free";
    if (!provider_execute) return "db_provider_execute";
    if (!event_allocator) return "event_allocator_global";
    if (!event_base_vtable) return "event_base_vtable";
    if (!inserted_vtable) return "player_inserted_event_vtable";
    if (!insert_team_player) return "dc_insert_team_player";
    if (!post_event) return "post_career_event";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

static std::string num(long long v) { return std::to_string(v); }

// ---------------------------------------------------------------- the payload
static bool good_column(const std::string& c) {
    if (c.empty() || c.size() > 47) return false;  // the query node keeps at most 47 characters
    for (char ch : c)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
    return true;
}

// One JSON number that must be a 32-bit integer
static std::string as_int(const json& v, const std::string& what, int64_t& out) {
    if (v.is_number_float()) return what + " is a float (" + v.dump() + "): only integers and strings are accepted";
    if (v.is_boolean() || v.is_null() || v.is_object() || v.is_array()) return what + " is not an integer (" + v.dump() + ")";
    if (v.is_string()) return what + " is a string: an integer is expected";
    if (v.is_number_unsigned()) {
        const uint64_t u = v.get<uint64_t>();
        if (u > static_cast<uint64_t>(kIntMax)) return what + " (" + v.dump() + ") does not fit a 32-bit integer";
        out = static_cast<int64_t>(u);
        return "";
    }
    if (!v.is_number_integer()) return what + " is not an integer";
    out = v.get<int64_t>();
    if (out < kIntMin || out > kIntMax) return what + " (" + v.dump() + ") does not fit a 32-bit integer";
    return "";
}

static std::string int_row(const json& obj, const char* table, Row& out) {
    if (!obj.is_object()) return std::string("\"") + table + "\" is not an object";
    if (obj.size() > kMaxColumns) return std::string("\"") + table + "\" has " + num(static_cast<long long>(obj.size())) + " columns (at most " + num(kMaxColumns) + ")";
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const std::string col = it.key();
        if (!good_column(col)) return std::string(table) + " column \"" + col.substr(0, 60) + "\" is not a column name";
        Value v;
        v.column = col;
        const std::string err = as_int(it.value(), std::string(table) + "." + col, v.i);
        if (!err.empty()) return err;
        out.push_back(v);
    }
    return "";
}

std::string parse_payload(const std::string& text, Payload& out) {
    out = Payload();
    if (text.size() > kMaxPayloadBytes) return "the payload is " + num(static_cast<long long>(text.size())) + " bytes (at most " + num(kMaxPayloadBytes) + ")";
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return "the payload is not a JSON object";
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (k != "seq" && k != "playerid" && k != "team" && k != "months" && k != "wage" && k != "players" && k != "names" && k != "link")
            return "unknown table or key \"" + k.substr(0, 60) + "\" in the payload (players, names and link are the only tables)";
    }
    struct Field { const char* key; int* dst; bool required; };
    const Field fields[] = {{"seq", &out.seq, true}, {"playerid", &out.playerid, true}, {"team", &out.team, true}, {"months", &out.months, false}, {"wage", &out.wage, false}};
    for (const auto& f : fields) {
        if (!j.contains(f.key)) {
            if (f.required) return std::string("the payload has no \"") + f.key + "\"";
            continue;
        }
        int64_t v = 0;
        const std::string err = as_int(j[f.key], f.key, v);
        if (!err.empty()) return err;
        *f.dst = static_cast<int>(v);
    }
    if (!j.contains("players")) return "the payload has no \"players\" row";
    Row players;
    std::string err = int_row(j["players"], "players", players);
    if (!err.empty()) return err;
    // playerid first (the INSERT names it), then the other columns in the object's order
    bool have_pid = false;
    for (const auto& v : players) {
        if (v.column != "playerid") continue;
        have_pid = true;
        if (v.i != out.playerid) return "players.playerid (" + num(v.i) + ") is not the payload's playerid (" + num(out.playerid) + ")";
        out.players.push_back(v);
    }
    if (!have_pid) {
        Value v;
        v.column = "playerid";
        v.i = out.playerid;
        out.players.push_back(v);
    }
    for (const auto& v : players)
        if (v.column != "playerid") out.players.push_back(v);
    if (j.contains("names")) {
        const json& n = j["names"];
        if (!n.is_object()) return "\"names\" is not an object";
        for (auto it = n.begin(); it != n.end(); ++it) {
            const std::string col = it.key();
            bool known = false;
            for (const auto& c : name_columns()) known = known || c == col;
            if (!known) return "unknown names column \"" + col.substr(0, 60) + "\" (firstname, surname, commonname, playerjerseyname)";
            if (!it.value().is_string()) return "names." + col + " is not a string";
            const std::string s = it.value().get<std::string>();
            if (s.size() > kMaxString) return "names." + col + " is " + num(static_cast<long long>(s.size())) + " bytes long (at most " + num(kMaxString) + ")";
            if (s.find('\0') != std::string::npos) return "names." + col + " holds a NUL character";
            if (s.empty()) continue;
            Value v;
            v.column = col;
            v.is_string = true;
            v.s = s;
            out.names.push_back(v);
        }
    }
    if (j.contains("link")) {
        Row link;
        err = int_row(j["link"], "link", link);
        if (!err.empty()) return err;
        for (const auto& v : link) {
            bool known = false;
            for (const auto& c : link_columns()) known = known || c == v.column;
            if (!known) return "unknown link column \"" + v.column + "\" (form is the only one)";
            out.link.push_back(v);
        }
    }
    return "";
}

Columns columns_from_meta(const DbMeta& meta) {
    Columns out;
    for (const auto& t : meta.tables) {
        auto& set = out[t.second.name];
        for (const auto& f : t.second.fields) set.insert(f.second.name);
    }
    return out;
}

// ---------------------------------------------------------------- locate
static bool in_image(uint64_t p, const Request& req) {
    if (!req.image_base || !req.image_size) return true;
    return p >= req.image_base && p < req.image_base + req.image_size;
}

static std::string fn_slot(Memory& mem, uint64_t vt, int slot, const Request& req, const std::string& what) {
    uint64_t fn = 0;
    if (!mem.rd(vt + static_cast<uint64_t>(slot) * 8, fn) || !is_ptr(fn, 1) || !in_image(fn, req))
        return what + " vtable " + hex(vt) + " slot " + num(slot) + " is not a function inside FC27.exe";
    return "";
}

std::string locate(Memory& mem, const Request& req, const Fns& fns, pm::Located& out) {
    out = pm::Located();
    if (const char* m = fns.missing()) return std::string("game function ") + m + " is not resolved on this game build";
    for (uint64_t p : {fns.query_init, fns.query_set_int, fns.query_set_string, fns.query_select, fns.query_where_int, fns.query_destroy, fns.result_free,
                       fns.provider_execute, fns.event_allocator, fns.event_base_vtable, fns.inserted_vtable, fns.insert_team_player, fns.post_event})
        if (!in_image(p, req)) return "resolved address " + hex(p) + " is outside FC27.exe";
    pm::Request mq;
    mq.action = pm::kActionMove;
    mq.comm = req.comm;
    mq.managers = req.managers;
    mq.image_base = req.image_base;
    mq.image_size = req.image_size;
    std::string err = pm::locate(mem, mq, fns.move, out);
    if (!err.empty()) return err;
    // the provider the queries run on must be the class whose Execute Turbo resolved (vtable slot 1)
    const uint64_t provider = mem.ptr(out.dc + pm::kDcProvider), pvt = provider ? mem.ptr(provider) : 0;
    uint64_t slot1 = 0;
    if (!pvt || !mem.rd(pvt + 8, slot1)) return "the DataController's db provider is not readable";
    if (slot1 != fns.provider_execute)
        return "the DataController's db provider vtable slot 1 (" + hex(slot1) + ") is not the Execute Turbo resolved (" + hex(fns.provider_execute) +
               "): not the provider the research describes";
    // the event allocator: a global in the image holding an object whose vtable (slot 2 = allocate) is in the image
    const uint64_t alloc = mem.ptr(fns.event_allocator), avt = alloc ? mem.ptr(alloc) : 0;
    if (!alloc || !avt || !in_image(avt, req)) return "the game's event allocator (*" + hex(fns.event_allocator) + ") has no vtable in FC27.exe";
    err = fn_slot(mem, avt, 2, req, "the event allocator's");
    if (!err.empty()) return err;
    // the 0x3A event vtable: not the base class, and the slots PostEvent calls (1 addref, 2 release, 4) are functions in the image
    if (fns.inserted_vtable == fns.event_base_vtable) return "the 0x3A event vtable is the event base vtable (layout mismatch)";
    for (int s : {0, 1, 2, 4}) {
        err = fn_slot(mem, fns.inserted_vtable, s, req, "the 0x3A event");
        if (!err.empty()) return err;
    }
    err = fn_slot(mem, fns.event_base_vtable, 0, req, "the event base");
    if (!err.empty()) return err;
    return "";
}

// ---------------------------------------------------------------- the sequence
static Result fail(Result r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg.size() < 511 ? msg : msg.substr(0, 507) + "...";
    return r;
}

static std::string exists_text(int w) {
    if (!w) return "nothing was written";
    std::string s;
    auto add = [&](const char* t) { s += (s.empty() ? "" : ", ") + std::string(t); };
    if (w & kWrotePlayers) add("players row");
    if (w & kWroteNames) add("editedplayernames row");
    if (w & kWroteEvent) add("event 0x3A posted");
    if (w & kWroteFaLink) add("Free Agents link");
    if (w & kWroteMoved) add("moved by PlayerMoved");
    if (w & kWroteContract) add("contract record");
    if (w & kWroteLink) add("link values");
    return "what exists now: " + s;
}

static std::string count_where(Caller& call, uint64_t dc, const char* table, const Where& where, int& rows) {
    std::vector<int64_t> none;
    std::string err;
    rows = -1;
    if (!call.select_ints(dc, table, {"playerid"}, where, rows, none, err)) return std::string("SELECT on ") + table + ": " + err;
    if (rows < 0) return std::string("SELECT on ") + table + " returned no count";
    return "";
}

// The row in statements of at most kColumnsPerStatement columns (the first one starts with playerid: the INSERT)
static std::vector<Row> chunks(const Row& row) {
    std::vector<Row> out;
    for (size_t i = 0; i < row.size();) {
        const size_t n = std::min(kColumnsPerStatement, row.size() - i);
        out.emplace_back(row.begin() + static_cast<long long>(i), row.begin() + static_cast<long long>(i + n));
        i += n;
    }
    return out;
}

static std::string mismatch_text(const std::vector<std::string>& bad) {
    if (bad.empty()) return "";
    std::string s = "; WARNING: " + num(static_cast<long long>(bad.size())) + " value(s) read back differently (";
    for (size_t i = 0; i < bad.size() && i < 3; ++i) s += (i ? ", " : "") + bad[i];
    return s + (bad.size() > 3 ? ", ...)" : ")");
}

// SELECT the row's int columns back in statements of kColumnsPerStatement; rows must be exactly 1
static std::string read_back(Caller& call, uint64_t dc, const char* table, const Row& row, const Where& where, std::vector<std::string>& bad) {
    std::vector<std::string> cols;
    std::vector<int64_t> want;
    for (const auto& v : row)
        if (!v.is_string) {
            cols.push_back(v.column);
            want.push_back(v.i);
        }
    for (size_t i = 0; i < cols.size(); i += kColumnsPerStatement) {
        const size_t n = std::min(kColumnsPerStatement, cols.size() - i);
        std::vector<std::string> part(cols.begin() + static_cast<long long>(i), cols.begin() + static_cast<long long>(i + n));
        std::vector<int64_t> got;
        int rows = -1;
        std::string err;
        if (!call.select_ints(dc, table, part, where, rows, got, err)) return std::string("the read-back SELECT on ") + table + " failed: " + err;
        if (rows != 1) return std::string("the read-back SELECT on ") + table + " found " + num(rows) + " rows (1 expected)";
        if (got.size() != n) return std::string("the read-back SELECT on ") + table + " returned " + num(static_cast<long long>(got.size())) + " values";
        for (size_t k = 0; k < n; ++k)
            if (got[k] != want[i + k]) bad.push_back(part[k] + " " + num(want[i + k]) + " -> " + num(got[k]));
    }
    return "";
}

Result run(Memory& mem, Caller& call, const Fns& fns, const Request& req) {
    Result r;
    if (!req.bad_args.empty()) return fail(r, "validate", req.bad_args);
    if (!valid_action(req.action)) return fail(r, "validate", "unknown player_create code " + num(req.action) + " (1 create, 9 check only)");
    if (!req.payload_error.empty()) return fail(r, "validate", std::string("payload turbo_output\\") + payload_name() + ": " + req.payload_error);
    const Payload& p = req.payload;
    const int pid = req.player;
    const std::string who = "player " + num(pid);
    if (p.seq != req.seq)
        return fail(r, "validate", "the payload file is for request #" + num(p.seq) + ", not #" + num(req.seq) + " (a stale file? nothing was written)");
    if (p.playerid != pid) return fail(r, "validate", "the payload is for player " + num(p.playerid) + ", the call for " + num(pid) + " (nothing was written)");
    if (pid <= 0 || pid >= kMaxPlayerId)
        return fail(r, "validate", "player id " + num(pid) + " is out of range (1 to " + num(kMaxPlayerId - 1) + "; " + num(kMaxPlayerId) + "+ is the game's own range)");
    if (pid == pm::kPlayerCareer) return fail(r, "validate", who + " is the player-career player's id: not used for a new player");
    if (p.team <= 0) return fail(r, "validate", "team id must be a positive number");
    if (p.months < 0 || p.months > pm::kMaxMonths) return fail(r, "validate", "contract length " + num(p.months) + " months is out of range (0 to 120)");
    if (p.wage < 0 || p.wage > pm::kMaxWage) return fail(r, "validate", "wage " + num(p.wage) + " is out of range (0 to 10,000,000)");
    if (p.players.size() < 2) return fail(r, "validate", "the players row has no columns besides playerid");
    // every column must be one the database has (bridge_meta.json): a wrong name would fail a statement half way
    if (req.columns.empty()) return fail(r, "validate", "the database columns are not known (turbo_output\\bridge_meta.json not loaded): nothing was written");
    auto known = [&](const char* table, const Row& row) -> std::string {
        const auto t = req.columns.find(table);
        if (t == req.columns.end()) return std::string("the database has no table ") + table;
        for (const auto& v : row)
            if (!t->second.count(v.column)) return std::string("unknown column ") + table + "." + v.column + " (not in this database)";
        return "";
    };
    std::string err = known("players", p.players);
    if (err.empty() && !p.names.empty()) err = known("editedplayernames", p.names);
    if (err.empty() && !p.link.empty()) err = known("teamplayerlinks", p.link);
    if (err.empty() && !req.columns.count("teamplayerlinks")) err = "the database has no table teamplayerlinks";
    if (!err.empty()) return fail(r, "validate", err + " (nothing was written)");
    for (const auto& v : p.players)
        if (v.is_string) return fail(r, "validate", "players." + v.column + " is a string: the players row takes integers only");
    err = locate(mem, req, fns, r.at);
    if (!err.empty()) return fail(r, "validate", err);
    err = svm::sim_busy(mem, r.at.managers);
    if (!err.empty()) return fail(r, "validate", err);
    const uint64_t dc = r.at.dc;
    // the id is new: no row in any of the three tables (the game's own SELECT)
    const Where by_pid = {{"playerid", pid}};
    for (const char* t : {"players", "teamplayerlinks", "editedplayernames"}) {
        if (!req.columns.count(t)) continue;
        int rows = -1;
        err = count_where(call, dc, t, by_pid, rows);
        if (!err.empty()) return fail(r, "validate", err);
        if (rows != 0)
            return fail(r, "validate", "player id " + num(pid) + " is in use: " + t + " has " + num(rows) + " row(s) for it (nothing was written)");
    }
    // the club: Free Agents or a club of this career with room for one more
    bool is_club = false;
    err = pm::check_team(call, r.at, p.team, "the new player's club", is_club);
    if (!err.empty()) return fail(r, "validate", err + " (nothing was written)");
    int rows_to = -1, loaned_to = -1, pending_to = -1;
    if (is_club) {
        std::string e2;
        if (!call.squad_counts(dc, p.team, rows_to, loaned_to, pending_to, e2)) return fail(r, "validate", "SquadCounts(team " + num(p.team) + "): " + e2);
        if (rows_to < 0 || loaned_to < 0 || pending_to < 0) return fail(r, "validate", "SquadCounts(team " + num(p.team) + ") left its outputs unset");
        const int total = rows_to + loaned_to + pending_to;
        if (total + 1 > r.at.max_squad)
            return fail(r, "validate", "team " + num(p.team) + "'s squad is full (" + num(rows_to) + " + loaned out " + num(loaned_to) + " + pre-signed " + num(pending_to) +
                                           ", MAX_SQUAD_SIZE " + num(r.at.max_squad) + "): nothing was written");
    }
    const bool user_club = p.team == r.at.user_team;
    const std::string dest = p.team == pm::kTeamFreeAgents ? std::string("Free Agents") : "team " + num(p.team) + (user_club ? " (your club)" : "");
    if (req.action == kActionCheck) {
        r.ok = true;
        r.stage = "done";
        r.in_team = 1;
        r.message = who + " can be created into " + dest + " (checked only, nothing was written): the id is free in players / teamplayerlinks / editedplayernames, " +
                    num(static_cast<long long>(p.players.size())) + " players columns" + (p.names.empty() ? "" : ", names") +
                    (is_club ? "; squad " + num(rows_to + loaned_to + pending_to) + " of " + num(r.at.max_squad) : "");
        return r;
    }

    // ---- 1. players: INSERT the first statement, UPDATE the rest, read everything back
    const std::vector<Row> parts = chunks(p.players);
    int count = -1;
    r.calls.push_back("insert players");
    if (!call.insert_row(dc, "players", parts[0], count, err)) return fail(r, "call", "INSERT players: " + err + " (nothing was written)");
    if (count != 1) {
        // the game's own CreatePlayer requires exactly 1; make sure nothing half-exists
        int rows = -1;
        const std::string e = count_where(call, dc, "players", by_pid, rows);
        if (e.empty() && rows == 0) return fail(r, "call", "the game's INSERT players affected " + num(count) + " rows (1 expected): nothing was written");
        r.written |= kWrotePlayers;
        return fail(r, "check", "the game's INSERT players affected " + num(count) + " rows (1 expected) and " + num(rows) + " row(s) exist for " + who + ": " +
                                    exists_text(r.written));
    }
    r.written |= kWrotePlayers;
    for (size_t i = 1; i < parts.size(); ++i) {
        r.calls.push_back("update players");
        if (!call.update_row(dc, "players", parts[i], by_pid, err)) return fail(r, "call", "UPDATE players (statement " + num(static_cast<long long>(i + 1)) + "): " + err + "; " + exists_text(r.written));
    }
    std::vector<std::string> bad;
    err = read_back(call, dc, "players", p.players, by_pid, bad);
    if (!err.empty()) return fail(r, "check", err + "; " + exists_text(r.written));
    std::string warn = mismatch_text(bad);
    // ---- 2. names
    if (!p.names.empty()) {
        Row names;
        Value v;
        v.column = "playerid";
        v.i = pid;
        names.push_back(v);
        names.insert(names.end(), p.names.begin(), p.names.end());
        count = -1;
        r.calls.push_back("insert editedplayernames");
        if (!call.insert_row(dc, "editedplayernames", names, count, err)) return fail(r, "call", "INSERT editedplayernames: " + err + "; " + exists_text(r.written));
        int rows = -1;
        err = count_where(call, dc, "editedplayernames", by_pid, rows);
        if (!err.empty()) return fail(r, "check", err + "; " + exists_text(r.written));
        if (rows == 1) r.written |= kWroteNames;
        if (count != 1 || rows != 1)
            return fail(r, "check", "the game's INSERT editedplayernames affected " + num(count) + " rows, " + num(rows) + " exist (1 expected); " + exists_text(r.written));
    }
    // ---- 3. event 0x3A exactly as CreatePlayer builds it
    uint64_t ev = 0;
    r.calls.push_back("alloc event");
    if (!call.alloc_event(kEventSize, ev, err)) return fail(r, "call", "the game's event allocator: " + err + "; " + exists_text(r.written));
    if (!is_ptr(ev, 8)) return fail(r, "call", "the game's event allocator returned no memory (" + hex(ev) + "); " + exists_text(r.written));
    if (!mem.wr(ev + kEvVtable, fns.inserted_vtable) || !mem.wr(ev + kEvRefcount, static_cast<int32_t>(0)) ||
        !mem.wr(ev + kEvId, static_cast<int32_t>(kEventPlayerInserted)) || !mem.wr(ev + kEvPid, static_cast<int32_t>(pid)))
        return fail(r, "call", "the event 0x3A at " + hex(ev) + " could not be written (not posted); " + exists_text(r.written));
    r.calls.push_back("post 0x3A");
    if (!call.post_event(r.at.dispatcher, kEventPlayerInserted, ev, err)) return fail(r, "call", "PostEvent(0x3A): " + err + "; " + exists_text(r.written));
    r.written |= kWroteEvent;
    // ---- 4. the Free Agents link through the game's InsertTeamPlayer (event 0x5F), read back with IsPlayerInTeam
    int ret = 0;
    r.calls.push_back("insert team player");
    if (!call.insert_team_player(dc, pid, pm::kTeamFreeAgents, kFaJersey, kFaPosition, 0, ret, err))
        return fail(r, "call", "DataController::InsertTeamPlayer: " + err + "; " + exists_text(r.written));
    bool in_fa = false;
    if (!call.in_team(dc, pid, pm::kTeamFreeAgents, in_fa, err)) return fail(r, "check", "IsPlayerInTeam(Free Agents) read-back: " + err + "; " + exists_text(r.written));
    if (!in_fa) {
        r.in_team = 0;
        r.final_team = pm::kTeamFreeAgents;
        return fail(r, "check", "after InsertTeamPlayer the game does not see " + who + " in Free Agents (IsPlayerInTeam false); " + exists_text(r.written));
    }
    r.written |= kWroteFaLink;
    r.final_team = pm::kTeamFreeAgents;
    std::string moved;
    // ---- 5. the club: op 11's whole move (squad limits, contract record for the user's club, read-backs)
    if (p.team != pm::kTeamFreeAgents) {
        pm::Request mq;
        mq.action = pm::kActionMove;
        mq.player = pid;
        mq.from = pm::kTeamFreeAgents;
        mq.to = p.team;
        mq.months = p.months;
        mq.wage = p.wage;
        mq.comm = req.comm;
        mq.managers = req.managers;
        mq.image_base = req.image_base;
        mq.image_size = req.image_size;
        r.calls.push_back("move");
        const pm::Result mr = pm::run(mem, call, fns.move, mq);
        if (mr.called) r.written |= kWroteMoved;
        if (mr.contract_called) r.written |= kWroteContract;
        if (!mr.ok) {
            r.in_team = mr.to_ok;
            return fail(r, mr.called ? "check" : "call", "created in Free Agents, but the move to " + dest + " failed: " + mr.message.substr(0, 220) + "; " + exists_text(r.written));
        }
        r.final_team = p.team;
        const size_t c = mr.message.find("; contract record");
        moved = " and moved to " + dest + (c != std::string::npos ? mr.message.substr(c, 90) : std::string());
    }
    // ---- 6. the link values (form) on the final link, read back
    if (!p.link.empty()) {
        const Where w = {{"playerid", pid}, {"teamid", r.final_team}};
        r.calls.push_back("update teamplayerlinks");
        if (!call.update_row(dc, "teamplayerlinks", p.link, w, err)) return fail(r, "call", "UPDATE teamplayerlinks: " + err + "; " + exists_text(r.written));
        std::vector<std::string> lbad;
        err = read_back(call, dc, "teamplayerlinks", p.link, w, lbad);
        if (!err.empty()) return fail(r, "check", err + "; " + exists_text(r.written));
        if (lbad.empty()) r.written |= kWroteLink;
        warn += mismatch_text(lbad);
    }
    // ---- 7. the final read-back
    bool in_final = false;
    if (!call.in_team(dc, pid, r.final_team, in_final, err)) return fail(r, "check", "the final IsPlayerInTeam read-back: " + err + "; " + exists_text(r.written));
    r.in_team = in_final ? 1 : 0;
    if (!in_final) return fail(r, "check", "the game does not see " + who + " in team " + num(r.final_team) + " at the end; " + exists_text(r.written));
    r.ok = true;
    r.stage = "done";
    std::string text = who + " created through the game's database (players " + num(static_cast<long long>(p.players.size())) + " columns read back" +
                       (p.names.empty() ? "" : ", names") + ", event 0x3A, Free Agents link)" + moved;
    for (const auto& v : p.link) text += "; " + v.column + " " + num(v.i);
    text += "; the game sees him in " + (r.final_team == pm::kTeamFreeAgents ? std::string("Free Agents") : "team " + num(r.final_team)) + warn;
    r.message = text.size() < 511 ? text : text.substr(0, 507) + "...";
    return r;
}

// ---------------------------------------------------------------- mailbox words, the payload file, the switches
Request request_from_args(const int64_t args[4]) {
    Request q;
    q.comm = static_cast<uint64_t>(args[0]);
    if (args[1] < 0 || args[1] > 0xFF) q.bad_args = "player_create: the code word has unknown bits set (1 create, 9 check expected)";
    q.action = static_cast<int>(args[1] & 0xFF);
    if (args[2] <= 0 || args[2] > kIntMax) q.bad_args = "player_create: the request number is not a positive 32-bit number";
    q.seq = static_cast<int>(args[2] & 0x7FFFFFFF);
    if (args[3] <= 0 || args[3] > kIntMax) {
        if (q.bad_args.empty()) q.bad_args = "player id must be a positive number";
    }
    q.player = static_cast<int>(args[3] & 0x7FFFFFFF);
    return q;
}

void args_from_request(const Request& req, int64_t args[4]) {
    args[0] = static_cast<int64_t>(req.comm);
    args[1] = req.action;
    args[2] = req.seq;
    args[3] = req.player;
}

static bool file_present(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

bool opted_in(const std::filesystem::path& turbo_output) { return file_present(turbo_output / opt_in_name()); }
bool killed(const std::filesystem::path& turbo_output) { return file_present(turbo_output / kill_switch_name()); }

void load_payload(const std::filesystem::path& turbo_output, Request& req) {
    const std::filesystem::path f = turbo_output / payload_name();
    std::error_code ec;
    const auto size = std::filesystem::file_size(f, ec);
    if (ec) {
        req.payload_error = "the file is missing";
        return;
    }
    if (size > kMaxPayloadBytes) {
        req.payload_error = "the file is " + num(static_cast<long long>(size)) + " bytes (at most " + num(kMaxPayloadBytes) + ")";
        return;
    }
    std::ifstream in(f, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    if (!in.good() && !in.eof()) {
        req.payload_error = "the file could not be read";
        return;
    }
    req.payload_error = parse_payload(ss.str(), req.payload);
    // the columns of this database (bridge_meta.json, written by Turbo's Lua side)
    std::ifstream mf(turbo_output / "bridge_meta.json", std::ios::binary);
    if (mf) {
        std::stringstream ms;
        ms << mf.rdbuf();
        DbMeta meta;
        if (Bridge::parse_meta(ms.str(), meta, nullptr)) req.columns = columns_from_meta(meta);
    }
}

}  // namespace pc
}  // namespace turbo
