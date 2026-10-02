#include "t3db.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace turbo {

const char* Field::type_name() const {
    switch (type) {
        case FieldType::Int: return "int";
        case FieldType::Float: return "float";
        case FieldType::String: return "string";
        default: return "unknown";
    }
}

std::string Value::to_string() const {
    char buf[64];
    switch (type) {
        case FieldType::Int:
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(i));
            return buf;
        case FieldType::Float:
            std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(f));
            return buf;
        case FieldType::String:
            return s;
        default:
            return "?";
    }
}

bool Value::operator==(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
        case FieldType::Int: return i == o.i;
        case FieldType::Float: return std::fabs(f - o.f) < 1e-6f;
        case FieldType::String: return s == o.s;
        default: return true;
    }
}

const Field* Table::field(const std::string& n) const {
    auto it = by_name.find(n);
    if (it == by_name.end()) return nullptr;
    return &fields[static_cast<size_t>(it->second)];
}

std::vector<std::string> Table::field_names() const {
    std::vector<std::string> out;
    out.reserve(fields.size());
    for (const auto& f : fields) out.push_back(f.name);
    std::sort(out.begin(), out.end());
    return out;
}

uint64_t extract_bits(const uint8_t* p, int start, int depth) {
    uint64_t v = 0;
    for (int b = 0; b < depth && b < 64; ++b) {
        int bit = start + b;
        if ((p[bit >> 3] >> (bit & 7)) & 1) v |= (uint64_t(1) << b);
    }
    return v;
}

void insert_bits(uint8_t* p, int start, int depth, uint64_t value) {
    for (int b = 0; b < depth && b < 64; ++b) {
        int bit = start + b;
        uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
        if ((value >> b) & 1) p[bit >> 3] |= mask;
        else p[bit >> 3] &= static_cast<uint8_t>(~mask);
    }
}

static int64_t raw_to_int(uint64_t raw, const Field& f) { return static_cast<int64_t>(raw) + f.min; }

static float int_to_float(int64_t v) {
    uint32_t bits = static_cast<uint32_t>(v);
    float out;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

// ---------------------------------------------------------------- Snapshot
bool Snapshot::load(Memory& mem, const Table& t) {
    table = &t;
    data.clear();
    valid.clear();
    size_t total = size_t(t.record_size) * t.written;
    if (t.written == 0 || t.record_size == 0) return true;
    if (!mem.read_block(t.first_record, total, data)) {
        data.clear();
        return false;
    }
    for (uint32_t i = 0; i < t.written; ++i) {
        uint8_t last = data[size_t(i) * t.record_size + t.record_size - 1];
        if ((last & 0x80) == 0) valid.push_back(i);
    }
    return true;
}

int64_t Snapshot::get_int(uint32_t idx, const Field& f) const {
    const uint8_t* rec = data.data() + size_t(idx) * table->record_size;
    if (f.byte_off() + (f.start_bit() + f.depth + 7) / 8 > table->record_size) return f.min;
    return raw_to_int(extract_bits(rec + f.byte_off(), f.start_bit(), f.depth), f);
}

float Snapshot::get_float(uint32_t idx, const Field& f) const { return int_to_float(get_int(idx, f)); }

std::string Snapshot::get_str(uint32_t idx, const Field& f) const {
    const uint8_t* rec = data.data() + size_t(idx) * table->record_size;
    size_t off = f.byte_off();
    size_t max = f.max_len();
    if (off >= table->record_size) return std::string();
    if (off + max > table->record_size) max = table->record_size - off;
    size_t len = 0;
    while (len < max && rec[off + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(rec + off), len);
}

Value Snapshot::get(uint32_t idx, const Field& f) const {
    switch (f.type) {
        case FieldType::Int: return Value::of_int(get_int(idx, f));
        case FieldType::Float: return Value::of_float(get_float(idx, f));
        case FieldType::String: return Value::of_str(get_str(idx, f));
        default: return Value();
    }
}

int64_t Snapshot::get_int(uint32_t idx, const std::string& field, int64_t def) const {
    const Field* f = table ? table->field(field) : nullptr;
    if (!f || f->type != FieldType::Int) return def;
    return get_int(idx, *f);
}

// ---------------------------------------------------------------- Database
bool Database::parse_table(uint64_t hdr, const DbMeta& meta, Table& out) {
    char sn[5] = {0};
    if (!mem_.read(hdr + 0x40, sn, 4)) return false;
    std::string shortname(sn, 4);
    auto mt = meta.tables.find(shortname);
    if (mt == meta.tables.end()) return false;

    uint64_t first = 0;
    int32_t rec_size = 0;
    uint16_t written = 0;
    uint8_t cols = 0;
    if (!mem_.rd(hdr + 0x30, first) || !mem_.rd(hdr + 0x44, rec_size) || !mem_.rd(hdr + 0x7C, written) ||
        !mem_.rd(hdr + 0x82, cols))
        return false;
    if (rec_size <= 0 || rec_size > 65536) return false;
    if (written > 0 && !is_ptr(first)) return false;

    out = Table();
    out.name = mt->second.name;
    out.shortname = shortname;
    out.header = hdr;
    out.first_record = first;
    out.record_size = static_cast<uint32_t>(rec_size);
    out.written = written;

    std::vector<uint8_t> coldata;
    if (!mem_.read_block(hdr + 0x84, size_t(cols) * 0x10, coldata)) return false;
    for (int c = 0; c < cols; ++c) {
        const uint8_t* cd = coldata.data() + c * 0x10;
        int32_t type;
        int32_t bitoff;
        std::memcpy(&type, cd, 4);
        std::memcpy(&bitoff, cd + 4, 4);
        std::string fsn(reinterpret_cast<const char*>(cd + 8), 4);
        auto fm = mt->second.fields.find(fsn);
        if (fm == mt->second.fields.end()) continue;  // column without meta: skip like Live Editor would fail on it
        Field f;
        f.name = fm->second.name;
        f.shortname = fsn;
        f.raw_type = type;
        f.type = type == 3 ? FieldType::Int : type == 4 ? FieldType::Float : type == 0 ? FieldType::String : FieldType::Unknown;
        f.bit_offset = static_cast<uint32_t>(bitoff);
        f.depth = fm->second.depth;
        f.min = fm->second.min;
        if (f.byte_off() >= out.record_size) continue;
        out.by_name[f.name] = static_cast<int>(out.fields.size());
        out.fields.push_back(f);
    }
    return !out.fields.empty();
}

int Database::refresh(uint64_t db_service, const DbMeta& meta, std::string* err) {
    tables_.clear();
    service_ = db_service;
    if (!is_ptr(db_service)) {
        if (err) *err = "database service address not known yet";
        return 0;
    }
    uint64_t db = mem_.chain(db_service, {0x20, 0x08, 0x10});
    if (!db) {
        if (err) *err = "database chain is not readable";
        return 0;
    }
    std::set<uint64_t> seen;
    int guard_db = 0;
    while (db && guard_db++ < 64) {
        uint64_t t = mem_.ptr(db + 0x10);
        int guard_t = 0;
        while (t && guard_t++ < 4096 && !seen.count(t)) {
            seen.insert(t);
            Table tbl;
            if (parse_table(t, meta, tbl) && !tables_.count(tbl.name)) tables_[tbl.name] = tbl;
            t = mem_.ptr(t + 0x08);
        }
        db = mem_.ptr(db + 0x18);
    }
    if (tables_.empty() && err) *err = "no tables matched the database meta";
    return static_cast<int>(tables_.size());
}

const Table* Database::table(const std::string& name) const {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : &it->second;
}

std::vector<std::string> Database::table_names() const {
    std::vector<std::string> out;
    for (const auto& kv : tables_) out.push_back(kv.first);
    return out;
}

bool Database::record_valid(const Table& t, uint64_t rec) {
    uint8_t last = 0;
    if (!mem_.rd(rec + t.record_size - 1, last)) return false;
    return (last & 0x80) == 0;
}

bool Database::table_alive(const Table& t, uint64_t rec) {
    char sn[4];
    uint64_t first = 0;
    int32_t rec_size = 0;
    uint16_t written = 0;
    if (!mem_.read(t.header + 0x40, sn, 4) || std::string(sn, 4) != t.shortname) return false;
    if (!mem_.rd(t.header + 0x30, first) || first != t.first_record) return false;
    if (!mem_.rd(t.header + 0x44, rec_size) || static_cast<uint32_t>(rec_size) != t.record_size) return false;
    if (!mem_.rd(t.header + 0x7C, written)) return false;
    if (rec < t.first_record) return false;
    uint64_t off = rec - t.first_record;
    if (off % t.record_size != 0) return false;
    return off / t.record_size < written;
}

bool Database::get(const Table& t, uint64_t rec, const Field& f, Value& out) {
    (void)t;
    if (f.type == FieldType::String) {
        out = Value::of_str(mem_.read_cstr(rec + f.byte_off(), f.max_len()));
        return true;
    }
    size_t nbytes = static_cast<size_t>((f.start_bit() + f.depth + 7) / 8);
    std::vector<uint8_t> buf;
    if (!mem_.read_block(rec + f.byte_off(), nbytes, buf)) return false;
    int64_t v = raw_to_int(extract_bits(buf.data(), f.start_bit(), f.depth), f);
    if (f.type == FieldType::Float) out = Value::of_float(int_to_float(v));
    else out = Value::of_int(v);
    return true;
}

int64_t Database::get_int(const Table& t, uint64_t rec, const std::string& field, int64_t def) {
    const Field* f = t.field(field);
    if (!f || f->type != FieldType::Int) return def;
    Value v;
    if (!get(t, rec, *f, v)) return def;
    return v.i;
}

std::string Database::validate(const Field& f, const Value& v) {
    char buf[160];
    switch (f.type) {
        case FieldType::Int:
            if (v.type != FieldType::Int) return f.name + " expects an integer";
            if (v.i < f.min || v.i > f.max()) {
                std::snprintf(buf, sizeof(buf), "%s=%lld is outside the field range %lld..%lld", f.name.c_str(),
                              static_cast<long long>(v.i), static_cast<long long>(f.min), static_cast<long long>(f.max()));
                return buf;
            }
            return "";
        case FieldType::Float:
            if (v.type != FieldType::Float || !std::isfinite(v.f)) return f.name + " expects a number";
            return "";
        case FieldType::String:
            if (v.type != FieldType::String) return f.name + " expects text";
            if (f.max_len() == 0 || v.s.size() >= f.max_len()) {
                std::snprintf(buf, sizeof(buf), "%s accepts at most %d bytes", f.name.c_str(),
                              static_cast<int>(f.max_len()) - 1);
                return buf;
            }
            return "";
        default:
            return f.name + " has an unsupported type";
    }
}

std::string Database::parse(const Field& f, const std::string& text, Value& out) {
    if (f.type == FieldType::String) {
        out = Value::of_str(text);
        return validate(f, out);
    }
    const char* s = text.c_str();
    char* end = nullptr;
    if (f.type == FieldType::Int) {
        long long v = std::strtoll(s, &end, 10);
        if (end == s || *end != '\0') return f.name + " expects an integer";
        out = Value::of_int(v);
        return validate(f, out);
    }
    if (f.type == FieldType::Float) {
        float v = std::strtof(s, &end);
        if (end == s || *end != '\0') return f.name + " expects a number";
        out = Value::of_float(v);
        return validate(f, out);
    }
    return f.name + " has an unsupported type";
}

bool Database::set(const Table& t, uint64_t rec, const Field& f, const Value& v, std::string* err) {
    std::string why = validate(f, v);
    if (!why.empty()) {
        if (err) *err = why;
        return false;
    }
    if (!table_alive(t, rec)) {
        if (err) *err = "the database changed (save loaded?) - press Refresh";
        return false;
    }
    if (!record_valid(t, rec)) {
        if (err) *err = "record is not valid any more";
        return false;
    }
    if (f.type == FieldType::String) {
        std::vector<uint8_t> buf(f.max_len(), 0);
        std::memcpy(buf.data(), v.s.data(), v.s.size());
        if (!mem_.write(rec + f.byte_off(), buf.data(), buf.size())) {
            if (err) *err = "memory write failed";
            return false;
        }
        return true;
    }
    int64_t iv = v.i;
    if (f.type == FieldType::Float) {
        uint32_t bits;
        std::memcpy(&bits, &v.f, 4);
        iv = static_cast<int64_t>(static_cast<int32_t>(bits));
    }
    size_t nbytes = static_cast<size_t>((f.start_bit() + f.depth + 7) / 8);
    std::vector<uint8_t> buf;
    if (!mem_.read_block(rec + f.byte_off(), nbytes, buf)) {
        if (err) *err = "memory read failed";
        return false;
    }
    uint64_t raw = static_cast<uint64_t>(iv - f.min);
    insert_bits(buf.data(), f.start_bit(), f.depth, raw);
    if (!mem_.write(rec + f.byte_off(), buf.data(), buf.size())) {
        if (err) *err = "memory write failed";
        return false;
    }
    return true;
}

bool Database::set_int(const Table& t, uint64_t rec, const std::string& field, int64_t v, std::string* err) {
    const Field* f = t.field(field);
    if (!f) {
        if (err) *err = "no field " + field + " in " + t.name;
        return false;
    }
    return set(t, rec, *f, Value::of_int(v), err);
}

uint64_t Database::find(const Table& t, const std::string& field, int64_t v) {
    const Field* f = t.field(field);
    if (!f || f->type != FieldType::Int) return 0;
    Snapshot snap;
    if (!snap.load(mem_, t)) return 0;
    for (uint32_t idx : snap.valid) {
        if (snap.get_int(idx, *f) == v) return snap.addr(idx);
    }
    return 0;
}

}  // namespace turbo
