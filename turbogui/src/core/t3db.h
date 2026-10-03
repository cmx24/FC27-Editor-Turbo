// FC 27 LE Turbo GUI - reader/writer for the game's in-memory T3DB database.
// Layout (same one Live Editor's open Lua library lua\libs\v2\imports\t3db\*.lua walks):
//   DB service -> [+0x20] -> [+0x08] -> [+0x10] = first database node
//   database node: +0x10 first table, +0x18 next database
//   table header:  +0x08 next table, +0x30 first record, +0x40 shortname[4], +0x44 record size,
//                  +0x7C written records (u16), +0x82 column count (u8), +0x84 columns (0x10 each:
//                  +0x0 type, +0x4 bit offset, +0x8 shortname[4])
//   record: bit-packed fields; last byte bit 0x80 set = deleted record
// Field names, bit depths and minimum values come from the database meta Live Editor exposes to Lua
// (GetDBMeta), handed over through the Turbo bridge.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {

enum class FieldType { String, Int, Float, Unknown };

struct FieldMeta {
    std::string name;
    int depth = 0;
    int64_t min = 0;
};

struct TableMeta {
    std::string name;
    std::map<std::string, FieldMeta> fields;  // key: field shortname
};

struct DbMeta {
    std::map<std::string, TableMeta> tables;  // key: table shortname
    bool empty() const { return tables.empty(); }
};

struct Field {
    std::string name;
    std::string shortname;
    FieldType type = FieldType::Unknown;
    int32_t raw_type = -1;
    uint32_t bit_offset = 0;
    int depth = 0;
    int64_t min = 0;

    uint32_t byte_off() const { return bit_offset / 8; }
    int start_bit() const { return static_cast<int>(bit_offset % 8); }
    int64_t max() const { return depth >= 1 && depth <= 62 ? min + ((int64_t(1) << depth) - 1) : min; }
    size_t max_len() const { return static_cast<size_t>(depth / 8); }
    const char* type_name() const;
};

struct Value {
    FieldType type = FieldType::Unknown;
    int64_t i = 0;
    float f = 0.0f;
    std::string s;

    static Value of_int(int64_t v) { Value x; x.type = FieldType::Int; x.i = v; return x; }
    static Value of_float(float v) { Value x; x.type = FieldType::Float; x.f = v; return x; }
    static Value of_str(const std::string& v) { Value x; x.type = FieldType::String; x.s = v; return x; }
    std::string to_string() const;
    bool operator==(const Value& o) const;
};

class Table {
public:
    std::string name;
    std::string shortname;
    uint64_t header = 0;
    uint64_t first_record = 0;
    uint32_t record_size = 0;
    uint32_t written = 0;
    std::vector<Field> fields;
    std::map<std::string, int> by_name;

    const Field* field(const std::string& n) const;
    bool has(const std::string& n) const { return field(n) != nullptr; }
    uint64_t record_addr(uint32_t idx) const { return first_record + uint64_t(record_size) * idx; }
    std::vector<std::string> field_names() const;
};

// One bulk copy of a table's records, for fast list building and searching.
class Snapshot {
public:
    const Table* table = nullptr;
    std::vector<uint8_t> data;
    std::vector<uint32_t> valid;  // indexes of non-deleted records

    bool load(Memory& mem, const Table& t);
    int64_t get_int(uint32_t idx, const Field& f) const;
    float get_float(uint32_t idx, const Field& f) const;
    std::string get_str(uint32_t idx, const Field& f) const;
    Value get(uint32_t idx, const Field& f) const;
    int64_t get_int(uint32_t idx, const std::string& field, int64_t def = 0) const;
    uint64_t addr(uint32_t idx) const { return table->record_addr(idx); }
};

// Extract `depth` bits starting at bit `start` of a little-endian byte buffer
uint64_t extract_bits(const uint8_t* p, int start, int depth);
// Replace `depth` bits starting at bit `start`
void insert_bits(uint8_t* p, int start, int depth, uint64_t value);

class Database {
public:
    explicit Database(Memory& mem) : mem_(mem) {}

    // Walk the database from the DB service address. Returns number of tables found.
    int refresh(uint64_t db_service, const DbMeta& meta, std::string* err = nullptr);
    bool ready() const { return !tables_.empty(); }
    uint64_t service() const { return service_; }

    const Table* table(const std::string& name) const;
    std::vector<std::string> table_names() const;

    bool record_valid(const Table& t, uint64_t rec);
    // The table header still describes the same table and rec lies inside its record block.
    // Guards against writing into memory freed by a database reload (e.g. loading another save).
    bool table_alive(const Table& t, uint64_t rec);
    bool get(const Table& t, uint64_t rec, const Field& f, Value& out);
    int64_t get_int(const Table& t, uint64_t rec, const std::string& field, int64_t def = 0);

    // "" when the value is acceptable for the field, else a readable reason
    static std::string validate(const Field& f, const Value& v);
    // Parse text typed by the user into a value for this field; "" on success
    static std::string parse(const Field& f, const std::string& text, Value& out);

    bool set(const Table& t, uint64_t rec, const Field& f, const Value& v, std::string* err = nullptr);
    bool set_int(const Table& t, uint64_t rec, const std::string& field, int64_t v, std::string* err = nullptr);

    // First valid record where field == v (integer fields), 0 if none
    uint64_t find(const Table& t, const std::string& field, int64_t v);

    Memory& memory() { return mem_; }

private:
    bool parse_table(uint64_t hdr, const DbMeta& meta, Table& out);

    Memory& mem_;
    std::map<std::string, Table> tables_;
    uint64_t service_ = 0;
};

}  // namespace turbo
