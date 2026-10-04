// FC 27 LE Turbo GUI - the loaded commentary bank's selection tables (see commentary_bank.h, docs/callnames.md §6)
#include "commentary_bank.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "callnames.h"
#include "nlohmann/json.hpp"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

static uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
static uint64_t rd64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

bool parse_bank_row(const uint8_t* p, BankRow& out) {
    // +0x04, +0x14, +0x3c zero; +0x10 intensity 1..15; +0x1c tag 0x88 in the low byte; +0x38 cm_sim 1..15;
    // the four tagged pointers (+0x08, +0x20, +0x28, +0x30) are user-mode addresses with both low bits set
    if (rd32(p + 0x04) != 0 || rd32(p + 0x14) != 0 || rd32(p + 0x3c) != 0) return false;
    const uint32_t intensity = rd32(p + 0x10);
    if (intensity == 0 || intensity > 15) return false;
    const uint32_t tag = rd32(p + 0x1c);
    if ((tag & 0xff) != kBankRowTag) return false;
    const uint32_t cm = rd32(p + 0x38);
    if (cm == 0 || cm > 15) return false;
    for (size_t off : {size_t(0x08), size_t(0x20), size_t(0x28), size_t(0x30)}) {
        const uint64_t ptr = rd64(p + off);
        if ((ptr & 3) != 3 || !is_ptr(ptr & ~uint64_t(3))) return false;
    }
    const uint32_t value = rd32(p);
    if (value == 0 || value > 0x0FFFFFFF) return false;
    out.value = value;
    out.intensity = intensity;
    out.variation = rd32(p + 0x18);
    out.index = tag >> 8;
    out.cm = cm;
    return true;
}

size_t find_bank_rows(const uint8_t* buf, size_t len, uint64_t base, std::vector<BankRow>& out) {
    if (!buf || len < kBankRowSize) return 0;
    size_t n = 0;
    // start at the first 16-byte aligned address inside the buffer
    uint64_t first = (base + kBankRowAlign - 1) / kBankRowAlign * kBankRowAlign;
    for (size_t off = size_t(first - base); off + kBankRowSize <= len; off += kBankRowAlign) {
        const uint8_t* p = buf + off;
        // cheap rejects first: the tag byte and the intensity
        if (p[0x1c] != kBankRowTag || p[0x11] != 0 || p[0x12] != 0 || p[0x13] != 0) continue;
        BankRow r;
        if (!parse_bank_row(p, r)) continue;
        r.addr = base + off;
        out.push_back(r);
        ++n;
    }
    return n;
}

const char* bank_table_kind_name(BankTableKind k) {
    switch (k) {
        case BankTableKind::Surnames: return "surnames";
        case BankTableKind::Players: return "players";
        default: return "unknown";
    }
}

BankTableKind classify_bank_table(uint32_t min_value, uint32_t max_value, size_t rows) {
    if (rows < kBankMinTableRows) return BankTableKind::Unknown;
    if (min_value >= uint32_t(kCallnameMin) && max_value <= uint32_t(kCallnameMax)) return BankTableKind::Surnames;
    if (min_value >= 1 && max_value <= kBankMaxPlayerId) return BankTableKind::Players;
    return BankTableKind::Unknown;
}

std::vector<BankTable> group_bank_tables(std::vector<BankRow>& rows, Memory* mem, std::vector<size_t>* row_table) {
    std::sort(rows.begin(), rows.end(), [](const BankRow& a, const BankRow& b) { return a.addr < b.addr; });
    std::vector<BankTable> out;
    if (row_table) row_table->assign(rows.size(), 0);
    size_t i = 0;
    while (i < rows.size()) {
        size_t j = i + 1;
        while (j < rows.size() && rows[j].addr == rows[j - 1].addr + kBankRowSize) ++j;
        BankTable t;
        t.start = rows[i].addr;
        t.end = rows[j - 1].addr + kBankRowSize;
        t.rows = j - i;
        std::set<uint32_t> vals;
        t.min_value = rows[i].value;
        t.max_value = rows[i].value;
        for (size_t k = i; k < j; ++k) {
            vals.insert(rows[k].value);
            t.min_value = std::min(t.min_value, rows[k].value);
            t.max_value = std::max(t.max_value, rows[k].value);
            if (row_table) (*row_table)[k] = out.size();
        }
        t.distinct = vals.size();
        if (mem) {
            uint32_t h = 0;
            if (mem->rd(t.start - 4, h)) {
                t.header = h & 0x7fffffffu;
                t.header_ok = t.header == t.rows;
            }
        }
        t.kind = classify_bank_table(t.min_value, t.max_value, t.rows);
        out.push_back(t);
        i = j;
    }
    return out;
}

BankCapture capture_commentary_bank(Memory& mem, const std::vector<Region>& regions, const std::function<bool()>& cancelled,
                                    const std::function<double()>& clock, size_t chunk) {
    BankCapture c;
    const double t0 = clock ? clock() : 0.0;
    if (chunk < kBankRowSize * 2) chunk = kBankRowSize * 2;
    std::vector<uint8_t> buf;
    std::vector<BankRow> rows;
    for (const Region& g : regions) {
        if (g.end <= g.start) continue;
        ++c.regions;
        // overlapping chunks: a row that straddles a chunk border is seen whole in the next chunk
        for (uint64_t a = g.start; a < g.end;) {
            if (cancelled && cancelled()) {
                c.cancelled = true;
                c.note = "cancelled";
                c.seconds = clock ? clock() - t0 : 0.0;
                return c;
            }
            const uint64_t want = std::min<uint64_t>(chunk, g.end - a);
            if (!mem.read_block(a, size_t(want), buf)) {
                a += want;  // unreadable: skip the chunk
                continue;
            }
            c.bytes += want;
            find_bank_rows(buf.data(), buf.size(), a, rows);
            if (want < chunk) break;
            a += want - (kBankRowSize - kBankRowAlign);
        }
    }
    // the overlap can report a row twice: drop duplicates by address
    std::sort(rows.begin(), rows.end(), [](const BankRow& x, const BankRow& y) { return x.addr < y.addr; });
    rows.erase(std::unique(rows.begin(), rows.end(), [](const BankRow& x, const BankRow& y) { return x.addr == y.addr; }), rows.end());
    c.rows = rows.size();
    std::vector<size_t> row_table;
    c.tables = group_bank_tables(rows, &mem, &row_table);
    for (size_t k = 0; k < rows.size(); ++k) {
        const BankTable& t = c.tables[row_table[k]];
        if (t.kind == BankTableKind::Surnames) c.surnames.insert(rows[k].value);
    }
    for (const BankTable& t : c.tables) {
        if (t.kind != BankTableKind::Players) continue;
        std::set<uint32_t> seen;
        for (size_t k = 0; k < rows.size(); ++k)
            if (rows[k].addr >= t.start && rows[k].addr < t.end) seen.insert(rows[k].value);
        for (uint32_t v : seen) ++c.players[v];
    }
    c.seconds = clock ? clock() - t0 : 0.0;
    size_t surname_tables = 0, player_tables = 0;
    for (const auto& t : c.tables) {
        if (t.kind == BankTableKind::Surnames) ++surname_tables;
        if (t.kind == BankTableKind::Players) ++player_tables;
    }
    c.ok = !c.surnames.empty() || !c.players.empty();
    c.note = c.ok ? std::to_string(c.rows) + " rows in " + std::to_string(c.tables.size()) + " tables (" + std::to_string(surname_tables) +
                        " surname, " + std::to_string(player_tables) + " player-keyed): " + std::to_string(c.surnames.size()) +
                        " spoken surnames, " + std::to_string(c.players.size()) + " players with recordings"
                  : "no selection table found in memory (is a commentary bank loaded?)";
    return c;
}

fs::path spoken_cache_path(const fs::path& le_root, const std::string& lang) {
    return le_root / "turbo_output" / "callnames" / ("spoken_" + lang + ".json");
}

static json table_json(const BankTable& t) {
    char start[24], end[24];
    std::snprintf(start, sizeof(start), "0x%llX", static_cast<unsigned long long>(t.start));
    std::snprintf(end, sizeof(end), "0x%llX", static_cast<unsigned long long>(t.end));
    return json{{"start", start}, {"end", end}, {"rows", t.rows}, {"distinct", t.distinct}, {"min", t.min_value},
                {"max", t.max_value}, {"header", t.header}, {"header_ok", t.header_ok}, {"kind", bank_table_kind_name(t.kind)}};
}

std::string bank_cache_json(const BankCapture& c, const std::string& lang, const std::string& when, const std::string& build) {
    json j;
    j["turbo_spoken"] = 2;
    j["lang"] = lang;
    j["when"] = when;
    j["build"] = build;
    j["source"] = "live bank capture";
    j["note"] = c.note;
    j["rows"] = c.rows;
    j["regions"] = c.regions;
    j["bytes"] = c.bytes;
    j["seconds"] = c.seconds;
    std::vector<int64_t> s(c.surnames.begin(), c.surnames.end());
    std::sort(s.begin(), s.end());
    j["surnames"] = s;
    std::vector<std::pair<int64_t, int>> p(c.players.begin(), c.players.end());
    std::sort(p.begin(), p.end());
    json pl = json::array();
    for (const auto& kv : p) pl.push_back({kv.first, kv.second});
    j["players"] = pl;
    json tabs = json::array();
    for (const auto& t : c.tables)
        if (t.kind != BankTableKind::Unknown || t.rows >= kBankMinTableRows) tabs.push_back(table_json(t));
    j["tables"] = tabs;
    return j.dump(1);
}

static uint64_t hex_to_u64(const std::string& s) {
    return std::strtoull(s.c_str(), nullptr, 16);
}

bool parse_bank_cache_json(const std::string& text, BankCache& out, std::string* err) {
    out = BankCache{};
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("turbo_spoken", 0) != 2) {
        if (err) *err = "not a Turbo spoken-set cache";
        return false;
    }
    out.lang = j.value("lang", std::string());
    out.when = j.value("when", std::string());
    out.build = j.value("build", std::string());
    if (j.contains("surnames") && j["surnames"].is_array())
        for (const auto& v : j["surnames"])
            if (v.is_number_integer()) {
                int64_t id = v.get<int64_t>();
                if (id >= kCallnameMin && id <= kCallnameMax) out.surnames.insert(id);
            }
    if (j.contains("players") && j["players"].is_array())
        for (const auto& v : j["players"]) {
            if (v.is_array() && v.size() >= 2 && v[0].is_number_integer()) out.players[v[0].get<int64_t>()] = v[1].is_number_integer() ? v[1].get<int>() : 1;
            else if (v.is_number_integer()) out.players[v.get<int64_t>()] = 1;
        }
    if (j.contains("tables") && j["tables"].is_array())
        for (const auto& t : j["tables"]) {
            if (!t.is_object()) continue;
            BankTable b;
            b.start = hex_to_u64(t.value("start", std::string("0")));
            b.end = hex_to_u64(t.value("end", std::string("0")));
            b.rows = t.value("rows", size_t(0));
            b.distinct = t.value("distinct", size_t(0));
            b.min_value = t.value("min", 0u);
            b.max_value = t.value("max", 0u);
            b.header = t.value("header", 0u);
            b.header_ok = t.value("header_ok", false);
            std::string k = t.value("kind", std::string());
            b.kind = k == "surnames" ? BankTableKind::Surnames : k == "players" ? BankTableKind::Players : BankTableKind::Unknown;
            out.tables.push_back(b);
        }
    if (out.surnames.empty() && out.players.empty()) {
        if (err) *err = "the cache holds no spoken ids";
        return false;
    }
    size_t st = 0, pt = 0;
    for (const auto& t : out.tables) {
        if (t.kind == BankTableKind::Surnames) ++st;
        if (t.kind == BankTableKind::Players) ++pt;
    }
    out.source = "live bank capture " + out.when + " (" + std::to_string(st) + " surname + " + std::to_string(pt) +
                 " player tables): " + std::to_string(out.surnames.size()) + " spoken surnames, " +
                 std::to_string(out.players.size()) + " players with recordings";
    return true;
}

}  // namespace turbo
