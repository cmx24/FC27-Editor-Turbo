// FC 27 LE Turbo GUI - live league tables of the game's competition engine (see fce_standings.h).
#include "fce_standings.h"

#include <cstring>

namespace turbo {
namespace fce {

namespace {

uint16_t rd16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool vtable_is(Memory& mem, uint64_t obj, uint64_t image_base, uint64_t rva) {
    if (image_base == 0) return true;
    uint64_t vt = 0;
    return mem.rd(obj, vt) && vt == image_base + rva;
}

// Reads the two list headers; "" or the reason
std::string read_lists(Memory& mem, Located& loc) {
    loc.standings_list = mem.ptr(loc.manager + kOffStandingsList);
    if (!loc.standings_list) return "StandingsDataList pointer is not readable";
    uint64_t b = 0, e = 0;
    if (!mem.rd(loc.standings_list, b) || !mem.rd(loc.standings_list + 8, e)) return "StandingsDataList is not readable";
    if (b == 0 && e == 0) {
        loc.rows_begin = loc.rows_end = 0;
        loc.row_count = 0;
    } else {
        if (!is_ptr(b, 2) || !is_ptr(e, 2) || e < b || (e - b) % kStandingSize != 0) return "StandingsDataList has no vector shape";
        if ((e - b) / kStandingSize > kMaxRows) return "StandingsDataList is too large";
        loc.rows_begin = b;
        loc.rows_end = e;
        loc.row_count = uint32_t((e - b) / kStandingSize);
    }
    loc.fixture_list = mem.ptr(loc.manager + kOffFixtureList);
    if (!loc.fixture_list) return "FixtureDataList pointer is not readable";
    int32_t count = 0;
    uint64_t data = 0;
    if (!mem.rd(loc.fixture_list, count) || !mem.rd(loc.fixture_list + 8, data)) return "FixtureDataList is not readable";
    if (count < 0 || uint32_t(count) > kMaxFixtures) return "FixtureDataList count is out of range";
    if (count > 0 && !is_ptr(data, 2)) return "FixtureDataList data pointer is not readable";
    loc.fixture_count = uint32_t(count);
    loc.fixtures_data = count > 0 ? data : 0;
    // the competition tree is optional: an unreadable or odd list leaves it empty, the rows are still usable
    loc.compobj_list = mem.ptr(loc.manager + kOffCompObjList);
    loc.compobjs_data = 0;
    loc.compobj_count = 0;
    if (loc.compobj_list) {
        int32_t cap = 0, n = 0;
        uint64_t cdata = 0;
        if (mem.rd(loc.compobj_list, cap) && mem.rd(loc.compobj_list + 4, n) && mem.rd(loc.compobj_list + 8, cdata) && cap >= 0 && n >= 0 &&
            n <= cap && uint32_t(cap) <= kMaxCompObjs && (n == 0 || is_ptr(cdata, 2))) {
            loc.compobjs_data = n > 0 ? cdata : 0;
            loc.compobj_count = uint32_t(n);
        } else {
            loc.compobj_list = 0;
        }
    }
    return "";
}

std::string cstr_field(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0 && p[n] >= 0x20 && p[n] < 0x7F) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

}  // namespace

int CompObj::comp_number() const {
    if (short_name.size() < 2 || short_name[0] != 'C') return -1;
    int v = 0;
    for (size_t i = 1; i < short_name.size(); ++i) {
        if (short_name[i] < '0' || short_name[i] > '9') return -1;
        v = v * 10 + (short_name[i] - '0');
        if (v > 1000000) return -1;
    }
    return v;
}

bool decode_compobj(const uint8_t* p, CompObj& c) {
    c.id = rd16(p);
    c.parent = rd16(p + 4);
    c.type = p[6];
    c.short_name = cstr_field(p + 7, kCompObjShortLen);
    c.desc = cstr_field(p + 0x0E, kCompObjDescLen);
    c.used = p[0x2F];
    return true;
}

bool read_compobjs(Memory& mem, const Located& loc, std::vector<CompObj>& out) {
    out.clear();
    if (!loc.ok()) return false;
    if (loc.compobj_count == 0 || loc.compobjs_data == 0) return true;
    std::vector<uint8_t> buf;
    if (!mem.read_block(loc.compobjs_data, size_t(loc.compobj_count) * kCompObjSize, buf)) return false;
    out.resize(loc.compobj_count);
    for (uint32_t i = 0; i < loc.compobj_count; ++i) decode_compobj(buf.data() + size_t(i) * kCompObjSize, out[i]);
    return true;
}

bool describe_group(const std::vector<CompObj>& objs, uint16_t group, GroupInfo& out) {
    out = GroupInfo();
    out.group = group;
    if (group >= objs.size() || objs[group].used != 1 || objs[group].id != group) return false;
    uint16_t cur = group;
    for (int depth = 0; depth < 8; ++depth) {
        const CompObj& c = objs[cur];
        if (c.type == kCompTypeStage && !out.stage) {
            out.stage = cur;
            out.stage_desc = c.desc;
        } else if (c.type == kCompTypeCompetition && !out.comp) {
            out.comp = cur;
            out.comp_short = c.short_name;
            out.comp_desc = c.desc;
            out.comp_number = c.comp_number();
        } else if (c.type == kCompTypeNation && !out.nation) {
            out.nation = cur;
            out.nation_short = c.short_name;
        }
        const uint16_t parent = c.parent;
        if (parent == 0xFFFF || parent == cur || parent >= objs.size() || objs[parent].used != 1 || objs[parent].id != parent) break;
        cur = parent;
    }
    return true;
}

std::string locate(Memory& mem, uint64_t ifce, uint64_t image_base, Located& out) {
    out = Located();
    if (!is_ptr(ifce, 8)) return "no FCE interface pointer (bridge_state.json ifce)";
    if (!vtable_is(mem, ifce, image_base, kRvaInterfaceVtable)) return "the FCE interface object has another vtable (game update?)";
    Located loc;
    loc.impl = ifce;
    loc.hub = mem.ptr(ifce + kOffImplHub);
    if (!loc.hub) return "ManagerHub pointer is not readable";
    loc.connector = mem.ptr(loc.hub + kOffHubConnector);
    if (!loc.connector) return "DataConnector pointer is not readable (no career loaded?)";
    loc.manager = mem.ptr(loc.connector + kOffConnectorManager);
    if (!loc.manager) return "DataManager pointer is not readable";
    if (mem.ptr(loc.manager + kOffManagerConnector) != loc.connector) return "DataManager does not point back to its DataConnector";
    if (!vtable_is(mem, loc.manager, image_base, kRvaDataManagerVtable)) return "DataManager has another vtable (game update?)";
    std::string err = read_lists(mem, loc);
    if (!err.empty()) return err;
    out = loc;
    return "";
}

bool validate(Memory& mem, Located& loc, uint64_t image_base) {
    if (!loc.ok()) return false;
    if (mem.ptr(loc.impl + kOffImplHub) != loc.hub) return false;
    if (mem.ptr(loc.hub + kOffHubConnector) != loc.connector) return false;
    if (mem.ptr(loc.connector + kOffConnectorManager) != loc.manager) return false;
    if (mem.ptr(loc.manager + kOffManagerConnector) != loc.connector) return false;
    if (!vtable_is(mem, loc.manager, image_base, kRvaDataManagerVtable)) return false;
    Located fresh = loc;
    if (!read_lists(mem, fresh).empty()) return false;
    loc = fresh;  // the lists may have grown (new season); the chain is the same
    return true;
}

bool decode_row(const uint8_t* p, uint64_t addr, StandingRow& r) {
    r.addr = addr;
    r.id = rd16(p);
    r.compobj = rd16(p + 2);
    r.teamid = rd32(p + 4);
    r.teamindex = p[8];
    r.hw = p[0x09];
    r.hd = p[0x0A];
    r.hl = p[0x0B];
    r.hgf = p[0x0C];
    r.hga = p[0x0D];
    r.aw = p[0x0E];
    r.ad = p[0x0F];
    r.al = p[0x10];
    r.agf = p[0x11];
    r.aga = p[0x12];
    int16_t pts;
    std::memcpy(&pts, p + 0x14, 2);
    r.points = pts;
    r.used = p[0x16];
    return true;
}

void encode_counters(const StandingRow& r, uint8_t* p) {
    p[0x09] = r.hw;
    p[0x0A] = r.hd;
    p[0x0B] = r.hl;
    p[0x0C] = r.hgf;
    p[0x0D] = r.hga;
    p[0x0E] = r.aw;
    p[0x0F] = r.ad;
    p[0x10] = r.al;
    p[0x11] = r.agf;
    p[0x12] = r.aga;
    std::memcpy(p + 0x14, &r.points, 2);
}

bool decode_fixture(const uint8_t* p, uint64_t addr, Fixture& f) {
    f.addr = addr;
    f.date = rd32(p);
    f.time = rd16(p + 4);
    f.id = rd16(p + 6);
    f.compobj = rd16(p + 8);
    int16_t h, a;
    std::memcpy(&h, p + 0x0A, 2);
    std::memcpy(&a, p + 0x0C, 2);
    f.home_sid = h;
    f.away_sid = a;
    f.group = p[0x0E];
    f.home_score = int8_t(p[0x0F]);
    f.home_pens = int8_t(p[0x10]);
    f.away_score = int8_t(p[0x11]);
    f.away_pens = int8_t(p[0x12]);
    f.completion = p[0x13];
    f.used = p[0x14];
    return true;
}

bool read_rows(Memory& mem, const Located& loc, std::vector<StandingRow>& out) {
    out.clear();
    if (!loc.ok()) return false;
    if (loc.row_count == 0) return true;
    std::vector<uint8_t> buf;
    if (!mem.read_block(loc.rows_begin, size_t(loc.row_count) * kStandingSize, buf)) return false;
    out.resize(loc.row_count);
    for (uint32_t i = 0; i < loc.row_count; ++i) decode_row(buf.data() + size_t(i) * kStandingSize, loc.rows_begin + uint64_t(i) * kStandingSize, out[i]);
    return true;
}

bool read_fixtures(Memory& mem, const Located& loc, std::vector<Fixture>& out) {
    out.clear();
    if (!loc.ok()) return false;
    if (loc.fixture_count == 0) return true;
    std::vector<uint8_t> buf;
    if (!mem.read_block(loc.fixtures_data, size_t(loc.fixture_count) * kFixtureSize, buf)) return false;
    out.resize(loc.fixture_count);
    for (uint32_t i = 0; i < loc.fixture_count; ++i)
        decode_fixture(buf.data() + size_t(i) * kFixtureSize, loc.fixtures_data + uint64_t(i) * kFixtureSize, out[i]);
    return true;
}

std::string check_row(const StandingRow& row) {
    // the fields are u8 / s16 already; the checks guard the int arithmetic the callers do before building a row
    if (row.used != 1) return "the row is not in use";
    return "";
}

std::string write_row(Memory& mem, const Located& loc, const StandingRow& row) {
    if (!loc.ok()) return "the standings are not located";
    if (row.addr < loc.rows_begin || row.addr + kStandingSize > loc.rows_end || (row.addr - loc.rows_begin) % kStandingSize != 0)
        return "the row is outside the standings list (list changed? locate again)";
    std::string err = check_row(row);
    if (!err.empty()) return err;
    uint8_t cur[kStandingSize];
    if (!mem.read(row.addr, cur, kStandingSize)) return "the row is not readable";
    StandingRow now;
    decode_row(cur, row.addr, now);
    if (now.id != row.id || now.compobj != row.compobj || now.teamid != row.teamid || now.used != 1)
        return "the row changed under Turbo (new season or save loaded?) - reload the table";
    uint8_t img[kStandingSize];
    std::memcpy(img, cur, kStandingSize);
    encode_counters(row, img);
    if (std::memcmp(img, cur, kStandingSize) == 0) return "";
    if (!mem.write(row.addr + 0x09, img + 0x09, 0x16 - 0x09)) return "writing the row failed";
    return "";
}

namespace {
bool bump(uint8_t& v, int delta) {
    int n = int(v) + delta;
    if (n < 0 || n > 255) return false;
    v = uint8_t(n);
    return true;
}
bool bump16(int16_t& v, int delta) {
    int n = int(v) + delta;
    if (n < -32768 || n > 32767) return false;
    v = int16_t(n);
    return true;
}
}  // namespace

std::string apply_result(StandingRow& home, StandingRow& away, int hs, int as, const Points& pts, int sign) {
    if (hs < 0 || as < 0 || hs > 127 || as > 127) return "scores must be 0..127";
    if (sign != 1 && sign != -1) return "bad sign";
    StandingRow h = home, a = away;
    bool ok = true;
    switch (outcome_of(hs, as)) {
        case Outcome::HomeWin:
            ok = bump(h.hw, sign) && bump(a.al, sign) && bump16(h.points, sign * pts.win) && bump16(a.points, sign * pts.loss);
            break;
        case Outcome::AwayWin:
            ok = bump(h.hl, sign) && bump(a.aw, sign) && bump16(h.points, sign * pts.loss) && bump16(a.points, sign * pts.win);
            break;
        case Outcome::Draw:
            ok = bump(h.hd, sign) && bump(a.ad, sign) && bump16(h.points, sign * pts.draw) && bump16(a.points, sign * pts.draw);
            break;
    }
    ok = ok && bump(h.hgf, sign * hs) && bump(h.hga, sign * as) && bump(a.agf, sign * as) && bump(a.aga, sign * hs);
    if (!ok) return sign > 0 ? "a counter would pass 255 (or the points their limit)" : "a counter would drop below 0: the rows do not contain this result";
    home = h;
    away = a;
    return "";
}

std::string edit_result(Memory& mem, const Located& loc, uint16_t fixture_id, int new_home, int new_away, const Points& pts) {
    if (!loc.ok()) return "the standings are not located";
    if (fixture_id >= loc.fixture_count) return "no such fixture";
    if (new_home < 0 || new_away < 0 || new_home > 99 || new_away > 99) return "scores must be 0..99";
    uint64_t fa = loc.fixtures_data + uint64_t(fixture_id) * kFixtureSize;
    uint8_t fb[kFixtureSize];
    if (!mem.read(fa, fb, kFixtureSize)) return "the fixture is not readable";
    Fixture f;
    decode_fixture(fb, fa, f);
    if (f.used != 1 || f.id != fixture_id) return "the fixture slot is not in use";
    if (!f.played()) return "the match has not been played yet: Turbo only edits played results";
    if (f.home_sid < 0 || f.away_sid < 0 || uint32_t(f.home_sid) >= loc.row_count || uint32_t(f.away_sid) >= loc.row_count)
        return "the fixture's standing rows are out of range";
    if (f.home_sid == f.away_sid) return "the fixture has the same row on both sides";
    uint64_t ha = loc.rows_begin + uint64_t(f.home_sid) * kStandingSize, aa = loc.rows_begin + uint64_t(f.away_sid) * kStandingSize;
    uint8_t hb[kStandingSize], ab[kStandingSize];
    if (!mem.read(ha, hb, kStandingSize) || !mem.read(aa, ab, kStandingSize)) return "the standing rows are not readable";
    StandingRow home, away;
    decode_row(hb, ha, home);
    decode_row(ab, aa, away);
    if (home.used != 1 || away.used != 1) return "a standing row of this fixture is not in use";
    if (home.id != f.home_sid || away.id != f.away_sid) return "the standing rows do not match the fixture";
    if (f.home_score == new_home && f.away_score == new_away) return "";
    std::string err = apply_result(home, away, f.home_score, f.away_score, pts, -1);
    if (!err.empty()) return err;
    err = apply_result(home, away, new_home, new_away, pts, +1);
    if (!err.empty()) return err;
    // rows first (both or none as far as the checks go), then the fixture score bytes
    err = write_row(mem, loc, home);
    if (!err.empty()) return err;
    err = write_row(mem, loc, away);
    if (!err.empty()) return "home row written, away row failed: " + err;
    uint8_t score[1];
    score[0] = uint8_t(int8_t(new_home));
    if (!mem.write(fa + 0x0F, score, 1)) return "rows written, the fixture's home score failed";
    score[0] = uint8_t(int8_t(new_away));
    if (!mem.write(fa + 0x11, score, 1)) return "rows written, the fixture's away score failed";
    return "";
}

}  // namespace fce
}  // namespace turbo
