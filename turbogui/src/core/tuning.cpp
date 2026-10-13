// FC 27 LE Turbo GUI - gameplay tuning (see tuning.h)
#include "tuning.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#include "devops.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace tuning {

namespace fs = std::filesystem;
using nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace {

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

std::string num(double v) {
    char b[48];
    if (std::isfinite(v) && std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) std::snprintf(b, sizeof(b), "%.0f", v);
    else std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Writes through a .tmp file and a rename (a crash never leaves half a file)
bool write_file(const fs::path& p, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << text;
        if (!f) {
            if (err) *err = "write failed: " + tmp.string();
            return false;
        }
    }
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

// An integer given as a JSON number or as a string "0x1C" / "-0x10" / "28"
bool json_int(const json& v, int64_t& out) {
    if (v.is_number_integer()) {
        out = v.get<int64_t>();
        return true;
    }
    if (v.is_number_unsigned()) {
        const uint64_t u = v.get<uint64_t>();
        if (u > uint64_t(std::numeric_limits<int64_t>::max())) return false;
        out = int64_t(u);
        return true;
    }
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    bool neg = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        s = s.substr(1);
    }
    if (s.empty()) return false;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s.substr(2);
    }
    uint64_t acc = 0;
    for (char c : s) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) return false;
        if (acc > (uint64_t(std::numeric_limits<int64_t>::max()) - uint64_t(d)) / uint64_t(base)) return false;
        acc = acc * uint64_t(base) + uint64_t(d);
    }
    out = neg ? -int64_t(acc) : int64_t(acc);
    return true;
}

// A number (or null = not given)
bool json_num(const json& o, const char* key, bool& has, double& out, std::string& err, const std::string& who) {
    has = false;
    if (!o.contains(key) || o[key].is_null()) return true;
    if (!o[key].is_number()) {
        err = who + ": " + key + " must be a number or null";
        return false;
    }
    out = o[key].get<double>();
    if (!std::isfinite(out)) {
        err = who + ": " + key + " is not finite";
        return false;
    }
    has = true;
    return true;
}

std::string str_of(const json& o, const char* key) {
    if (!o.contains(key) || !o[key].is_string()) return "";
    return o[key].get<std::string>();
}

bool valid_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

std::string safe_name(const std::string& s) {
    std::string out;
    bool us = false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (ok) {
            out += c;
            us = false;
        } else if (!us && !out.empty()) {
            out += '_';
            us = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 60) out.resize(60);
    return out.empty() ? "preset" : out;
}

// No memory at all (a service without the game's memory): every read fails
class NullMemory : public Memory {
public:
    bool read(uint64_t, void*, size_t) override { return false; }
    bool write(uint64_t, const void*, size_t) override { return false; }
};

// Counts the reads made on the game's memory (the job reports them with its time)
class Counting : public Memory {
public:
    explicit Counting(Memory& m) : m_(m) {}
    bool read(uint64_t addr, void* out, size_t n) override {
        ++reads;
        return m_.read(addr, out, n);
    }
    bool write(uint64_t addr, const void* in, size_t n) override { return m_.write(addr, in, n); }
    int reads = 0;

private:
    Memory& m_;
};

}  // namespace

// ------------------------------------------------------------------------------------------------ names
std::vector<std::string> knobs_file_names(const std::string& build) {
    std::vector<std::string> out;
    if (build.empty()) return out;
    out.push_back("knobs_" + build + ".json");
    const size_t dash = build.find('-');
    if (dash != std::string::npos && dash > 0) out.push_back("knobs_" + build.substr(0, dash) + ".json");
    return out;
}

const char* type_name(ValueType t) {
    switch (t) {
        case ValueType::F32: return "f32";
        case ValueType::I32: return "i32";
        case ValueType::U8: return "u8";
        case ValueType::Bool: return "bool";
    }
    return "?";
}

bool type_from_name(const std::string& s, ValueType& t) {
    if (s == "f32") t = ValueType::F32;
    else if (s == "i32") t = ValueType::I32;
    else if (s == "u8") t = ValueType::U8;
    else if (s == "bool") t = ValueType::Bool;
    else return false;
    return true;
}

size_t type_size(ValueType t) { return t == ValueType::F32 || t == ValueType::I32 ? 4 : 1; }

const char* applies_name(Applies a) {
    switch (a) {
        case Applies::NextMatchSetup: return "next match setup";
        case Applies::Live: return "live";
        case Applies::Unknown: return "unknown";
    }
    return "unknown";
}

bool applies_from_name(const std::string& s, Applies& a) {
    if (s == "next match setup") a = Applies::NextMatchSetup;
    else if (s == "live") a = Applies::Live;
    else if (s == "unknown" || s.empty()) a = Applies::Unknown;
    else return false;
    return true;
}

const std::vector<std::string>& standard_groups() {
    static const std::vector<std::string> g = {"Shape with the ball", "Shape without the ball", "Defensive line depth bands", "Shape", "Pressing",
                                                "Marking", "Attacking runs", "Passing", "Shooting", "CPU AI", "Goalkeeper", "Roles"};
    return g;
}

const std::vector<std::string>& phases() {
    static const std::vector<std::string> p = {"hub", "prematch", "match"};
    return p;
}

const char* plan_kind_name(PlanKind k) {
    switch (k) {
        case PlanKind::Apply: return "apply";
        case PlanKind::Undo: return "undo";
        case PlanKind::Restore: return "restore game defaults";
        case PlanKind::Reapply: return "re-apply";
    }
    return "?";
}

// ------------------------------------------------------------------------------------------------ registry
bool Knob::allows(const std::string& phase) const { return std::find(when.begin(), when.end(), phase) != when.end(); }

bool Knob::game_value(const std::string& copy, double& v) const {
    auto it = copy_defaults.find(copy);
    if (it != copy_defaults.end()) {
        v = it->second;
        return true;
    }
    if (!has_default) return false;
    v = def;
    return true;
}

std::string Knob::placeholder() const {
    if (!verified && field.empty()) return "placeholder: no game field yet";
    if (!verified)
        return "unverified: " + (collection.empty() ? field : collection + "." + field) +
               " has not been confirmed in the running game yet; Turbo writes it only after a live check confirms it on this build";
    if (locator.empty()) return "no locator";
    if (offset < 0) return "no field offset";
    return "";
}

bool Locator::l3() const {
    for (const Variant& v : variants)
        if (!v.db.empty()) return true;
    return false;
}

std::string Locator::placeholder() const {
    if (!verified) return "placeholder: locator " + name + " has not been confirmed in the running game yet";
    if (kind == "chain" && signature.empty() && pattern.pattern.empty()) return "locator " + name + " has no signature";
    if (kind == "attrib") {
        if (type.empty()) return "locator " + name + " names no type";
        if (variants.empty()) return "locator " + name + " lists no variant";
        for (const Variant& v : variants)
            if (v.instance.empty()) return "locator " + name + ": variant " + v.name + " has no instance name yet";
    }
    return "";
}

std::string copy_key(const std::string& id, const std::string& copy) { return copy.empty() ? id : id + "@" + copy; }

const CopyAt* KnobLive::copy(const std::string& variant) const {
    for (const CopyAt& c : copies)
        if (c.variant == variant) return &c;
    return nullptr;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

const Knob* Registry::knob(const std::string& id) const {
    for (const Knob& k : knobs)
        if (k.id == id) return &k;
    return nullptr;
}

const Locator* Registry::locator(const std::string& name) const {
    for (const Locator& l : locators)
        if (l.name == name) return &l;
    return nullptr;
}

std::vector<std::string> Registry::groups() const {
    std::vector<std::string> out;
    for (const std::string& g : standard_groups())
        for (const Knob& k : knobs)
            if (k.group == g) {
                out.push_back(g);
                break;
            }
    for (const Knob& k : knobs)
        if (std::find(out.begin(), out.end(), k.group) == out.end()) out.push_back(k.group);
    return out;
}

int Registry::placeholders() const {
    int n = 0;
    for (const Knob& k : knobs) n += k.placeholder().empty() ? 0 : 1;
    return n;
}

static bool parse_check(const json& c, const Locator& l, HeaderCheck& out, std::string& err) {
    const std::string who = "locator " + l.name + " check";
    if (!c.is_object()) {
        err = who + ": not an object";
        return false;
    }
    int64_t v = 0;
    if (c.contains("object")) {
        if (!json_int(c["object"], v) || v < 0 || v > int64_t(l.chain.size())) {
            err = who + ": object must be 0.." + std::to_string(l.chain.size()) + " (the anchor's object, then one per chain step)";
            return false;
        }
        out.object = int(v);
    }
    if (c.contains("offset") && !c["offset"].is_null()) {
        if (!json_int(c["offset"], v)) {
            err = who + ": offset must be an integer or a hex string";
            return false;
        }
        out.offset = v;
    }
    out.kind = str_of(c, "kind");
    static const std::set<std::string> kinds = {"vtable", "ptr", "u8_range", "u32_range", "i32_range", "f32_range", "bytes"};
    if (!kinds.count(out.kind)) {
        err = who + ": unknown kind \"" + out.kind + "\" (vtable, ptr, u8_range, u32_range, i32_range, f32_range, bytes)";
        return false;
    }
    out.signature = str_of(c, "signature");
    out.bytes = str_of(c, "bytes");
    out.note = str_of(c, "note");
    if (out.kind == "vtable" && out.signature.empty()) {
        err = who + ": a vtable check names the signature of the vtable";
        return false;
    }
    if (out.kind == "bytes") {
        std::vector<uint8_t> b;
        std::vector<bool> m;
        if (!parse_pattern(out.bytes, b, m) || b.size() > 64) {
            err = who + ": bytes must be 1..64 hex bytes (?? allowed)";
            return false;
        }
    }
    if (out.kind.find("_range") != std::string::npos) {
        bool hmin = false, hmax = false;
        if (!json_num(c, "min", hmin, out.min, err, who) || !json_num(c, "max", hmax, out.max, err, who)) return false;
        if (!hmin || !hmax || out.min > out.max) {
            err = who + ": a range check needs min <= max";
            return false;
        }
    }
    return true;
}

static bool parse_locator(const json& o, Locator& l, std::string& err) {
    if (!o.is_object()) {
        err = "a locator is not an object";
        return false;
    }
    l.name = str_of(o, "name");
    if (!valid_id(l.name)) {
        err = "locator name \"" + l.name + "\" must be 1..64 of a-z 0-9 . _ -";
        return false;
    }
    const std::string who = "locator " + l.name;
    l.kind = o.contains("kind") ? str_of(o, "kind") : "chain";
    if (l.kind != "chain" && l.kind != "native" && l.kind != "attrib") {
        err = who + ": kind must be \"chain\", \"native\" or \"attrib\"";
        return false;
    }
    if (l.kind == "attrib") {
        l.type = str_of(o, "type");
        int64_t v = 0;
        if (o.contains("type_signature") && !o["type_signature"].is_null()) {
            if (!json_int(o["type_signature"], v) || v < 0 || v > 0xFFFFFFFFLL) {
                err = who + ": type_signature must be a u32 (hex string)";
                return false;
            }
            l.type_signature = uint32_t(v);
        }
        if (o.contains("type_size") && !o["type_size"].is_null()) {
            if (!json_int(o["type_size"], v) || v <= 0 || v > 0x100000) {
                err = who + ": type_size must be 1..0x100000";
                return false;
            }
            l.type_size = uint32_t(v);
        }
        if (o.contains("type_fields") && !o["type_fields"].is_null()) {
            if (!json_int(o["type_fields"], v) || v <= 0 || v > kMaxTypeFields) {
                err = who + ": type_fields must be 1.." + std::to_string(kMaxTypeFields);
                return false;
            }
            l.type_fields = int(v);
        }
        if (o.contains("variants") && !o["variants"].is_null()) {
            if (!o["variants"].is_array() || o["variants"].size() > 16) {
                err = who + ": variants must be a list of at most 16";
                return false;
            }
            for (const auto& x : o["variants"]) {
                Variant vr;
                vr.name = x.is_object() ? str_of(x, "name") : "";
                vr.instance = x.is_object() ? str_of(x, "instance") : "";
                vr.db = x.is_object() ? str_of(x, "db") : "";
                if (!valid_id(vr.name)) {
                    err = who + ": a variant name must be 1..64 of a-z 0-9 . _ -";
                    return false;
                }
                for (const Variant& o2 : l.variants)
                    if (o2.name == vr.name) {
                        err = who + ": variant " + vr.name + " twice";
                        return false;
                    }
                l.variants.push_back(vr);
            }
        }
        l.head = str_of(o, "head");
        if (l.head != "" && l.head != "image" && l.head != "typeinfo") {
            err = who + ": head must be \"image\" or \"typeinfo\"";
            return false;
        }
        if (o.contains("head_rva") && !o["head_rva"].is_null()) {
            if (!json_int(o["head_rva"], v) || v <= 0 || v > 0x7FFFFFFFLL) {
                err = who + ": head_rva must be an offset inside FC27.exe (hex string)";
                return false;
            }
            l.head_rva = uint64_t(v);
        }
        // L3: the database / group runtime objects
        l.db_type = str_of(o, "db_type");
        l.group_type = str_of(o, "group_type");
        for (const char* key : {"db_type_signature", "group_type_signature"}) {
            if (!o.contains(key) || o[key].is_null()) continue;
            if (!json_int(o[key], v) || v <= 0 || v > 0xFFFFFFFFLL) {
                err = who + ": " + key + " must be a u32 (hex string)";
                return false;
            }
            (std::string(key) == "db_type_signature" ? l.db_type_signature : l.group_type_signature) = uint32_t(v);
        }
        for (const char* key : {"family_slot", "group_slot"}) {
            if (!o.contains(key) || o[key].is_null()) continue;
            if (!json_int(o[key], v) || v < 0x18 || v > 0x10000 || (v & 7)) {
                err = who + ": " + key + " must be an 8-aligned offset 0x18..0x10000 (hex string)";
                return false;
            }
            (std::string(key) == "family_slot" ? l.family_slot : l.group_slot) = uint64_t(v);
        }
        if (l.l3() && (l.db_type.empty() || !l.family_slot || !l.group_slot)) {
            err = who + ": a variant names its database (L3): db_type, family_slot and group_slot are needed";
            return false;
        }
        l.verified = o.value("verified", false);
        l.note = str_of(o, "note");
        if (l.verified && (l.type.empty() || !l.type_signature || !l.type_size || l.type_fields <= 0 || l.variants.empty())) {
            err = who + ": an attrib locator marked verified needs type, type_signature, type_size, type_fields and variants";
            return false;
        }
        if (l.verified)
            for (const Variant& vr : l.variants)
                if (vr.instance.empty()) {
                    err = who + ": marked verified while variant " + vr.name + " has no instance name";
                    return false;
                }
        return true;
    }
    l.signature = str_of(o, "signature");
    l.pattern = Signature("tuning:" + l.name, str_of(o, "pattern"), o.contains("resolve") ? str_of(o, "resolve") : "none", 0, "", str_of(o, "expect"));
    if (o.contains("pattern_offset")) {
        int64_t v = 0;
        if (!json_int(o["pattern_offset"], v) || v < -4096 || v > 4096) {
            err = who + ": pattern_offset must be an integer -4096..4096";
            return false;
        }
        l.pattern.offset = int(v);
    }
    if (l.pattern.resolve != "none" && l.pattern.resolve != "rip") {
        err = who + ": resolve must be \"none\" or \"rip\"";
        return false;
    }
    if (!l.signature.empty() && !l.pattern.pattern.empty()) {
        err = who + ": give a signature name or an inline pattern, not both";
        return false;
    }
    if (!l.pattern.pattern.empty()) {
        std::vector<uint8_t> b;
        std::vector<bool> m;
        if (!parse_pattern(l.pattern.pattern, b, m)) {
            err = who + ": the pattern is not hex bytes (?? allowed)";
            return false;
        }
    }
    l.deref = o.value("deref", false);
    if (o.contains("chain") && !o["chain"].is_null()) {
        if (!o["chain"].is_array() || o["chain"].size() > size_t(kMaxChain)) {
            err = who + ": chain must be a list of at most " + std::to_string(kMaxChain) + " offsets";
            return false;
        }
        for (const auto& x : o["chain"]) {
            int64_t v = 0;
            if (!json_int(x, v) || v < -0x100000 || v > 0x100000) {
                err = who + ": a chain offset must be an integer (or hex string) within +-0x100000";
                return false;
            }
            l.chain.push_back(v);
        }
    }
    if (o.contains("add") && !o["add"].is_null()) {
        int64_t v = 0;
        if (!json_int(o["add"], v) || v < -0x100000 || v > 0x100000) {
            err = who + ": add must be an integer within +-0x100000";
            return false;
        }
        l.add = v;
    }
    if (o.contains("checks") && !o["checks"].is_null()) {
        if (!o["checks"].is_array()) {
            err = who + ": checks must be a list";
            return false;
        }
        for (const auto& c : o["checks"]) {
            HeaderCheck hc;
            if (!parse_check(c, l, hc, err)) return false;
            l.checks.push_back(hc);
        }
    }
    l.verified = o.value("verified", false);
    l.note = str_of(o, "note");
    if (l.verified && l.kind == "chain" && l.signature.empty() && l.pattern.pattern.empty()) {
        err = who + ": marked verified without a signature or a pattern";
        return false;
    }
    if (l.verified && l.kind == "chain" && l.checks.empty()) {
        err = who + ": marked verified without a header check (a pointer chain alone is never trusted)";
        return false;
    }
    return true;
}

static bool parse_knob(const json& o, const Registry& reg, Knob& k, std::string& err) {
    if (!o.is_object()) {
        err = "a knob is not an object";
        return false;
    }
    k.id = str_of(o, "id");
    if (!valid_id(k.id)) {
        err = "knob id \"" + k.id + "\" must be 1..64 of a-z 0-9 . _ -";
        return false;
    }
    const std::string who = "knob " + k.id;
    k.label = str_of(o, "label");
    k.group = str_of(o, "group");
    if (k.label.empty() || k.group.empty()) {
        err = who + ": label and group are needed";
        return false;
    }
    if (!type_from_name(str_of(o, "type"), k.type)) {
        err = who + ": type must be f32, i32, u8 or bool";
        return false;
    }
    k.collection = str_of(o, "collection");
    k.field = str_of(o, "field");
    k.unit = str_of(o, "unit");
    k.locator = str_of(o, "locator");
    k.notes = str_of(o, "notes");
    if (!applies_from_name(str_of(o, "applies"), k.applies)) {
        err = who + ": applies must be \"next match setup\", \"live\" or \"unknown\"";
        return false;
    }
    if (o.contains("when") && !o["when"].is_null()) {
        if (!o["when"].is_array() || o["when"].empty()) {
            err = who + ": when must be a non-empty list of phases (hub, prematch, match)";
            return false;
        }
        k.when.clear();
        for (const auto& p : o["when"]) {
            if (!p.is_string() || std::find(phases().begin(), phases().end(), p.get<std::string>()) == phases().end()) {
                err = who + ": when lists an unknown phase (hub, prematch, match)";
                return false;
            }
            k.when.push_back(p.get<std::string>());
        }
    }
    if (!json_num(o, "min", k.has_min, k.min, err, who) || !json_num(o, "max", k.has_max, k.max, err, who) ||
        !json_num(o, "default", k.has_default, k.def, err, who))
        return false;
    bool has_step = false;
    if (!json_num(o, "step", has_step, k.step, err, who)) return false;
    if (has_step && k.step < 0) {
        err = who + ": step must be >= 0";
        return false;
    }
    if (k.type == ValueType::Bool) {
        // a switch is 0 / 1 whatever the file says
        k.has_min = k.has_max = true;
        k.min = 0;
        k.max = 1;
    }
    if (k.has_min != k.has_max) {
        err = who + ": give both min and max (or neither)";
        return false;
    }
    if (k.has_min && k.min > k.max) {
        err = who + ": min > max";
        return false;
    }
    if (k.has_min && k.type == ValueType::U8 && (k.min < 0 || k.max > 255 || k.min != std::floor(k.min) || k.max != std::floor(k.max))) {
        err = who + ": a u8 range is whole numbers 0..255";
        return false;
    }
    if (k.has_min && k.type == ValueType::I32 &&
        (k.min < -2147483648.0 || k.max > 2147483647.0 || k.min != std::floor(k.min) || k.max != std::floor(k.max))) {
        err = who + ": an i32 range is whole numbers inside int32";
        return false;
    }
    if (k.has_min && k.type == ValueType::F32 && (std::fabs(k.min) > 3.0e38 || std::fabs(k.max) > 3.0e38)) {
        err = who + ": an f32 range must fit a float";
        return false;
    }
    if (k.has_default && k.has_min && (k.def < k.min || k.def > k.max)) {
        err = who + ": default outside min..max";
        return false;
    }
    if (k.has_default && (k.type != ValueType::F32 && k.def != std::floor(k.def))) {
        err = who + ": the default of an integer or a switch is a whole number";
        return false;
    }
    if (o.contains("copy_defaults") && !o["copy_defaults"].is_null()) {
        if (!o["copy_defaults"].is_object()) {
            err = who + ": copy_defaults must be an object {\"<variant>\": FC 27's value of that copy}";
            return false;
        }
        for (auto it = o["copy_defaults"].begin(); it != o["copy_defaults"].end(); ++it) {
            if (it.key().rfind('_', 0) == 0) continue;
            if (!valid_id(it.key()) || !it.value().is_number() || !std::isfinite(it.value().get<double>())) {
                err = who + ": copy_defaults: \"" + it.key() + "\" needs a variant name and a number";
                return false;
            }
            const double v = it.value().get<double>();
            if ((k.has_min && (v < k.min || v > k.max)) || (k.type != ValueType::F32 && v != std::floor(v))) {
                err = who + ": copy_defaults: " + it.key() + " is outside min..max or not a whole number";
                return false;
            }
            k.copy_defaults[it.key()] = v;
        }
    }
    if (o.contains("offset") && !o["offset"].is_null()) {
        int64_t v = 0;
        if (!json_int(o["offset"], v) || v < 0 || v > 0x100000) {
            err = who + ": offset must be 0..0x100000 (an integer or a hex string) or null";
            return false;
        }
        k.offset = v;
    }
    if (o.contains("below") && !o["below"].is_null()) {
        if (!o["below"].is_array()) {
            err = who + ": below must be a list of knob ids";
            return false;
        }
        for (const auto& b : o["below"]) {
            if (!b.is_string() || !valid_id(b.get<std::string>()) || b.get<std::string>() == k.id) {
                err = who + ": below lists a bad knob id";
                return false;
            }
            k.below.push_back(b.get<std::string>());
        }
    }
    k.verified = o.value("verified", false);
    const Locator* loc = k.locator.empty() ? nullptr : reg.locator(k.locator);
    if (!k.locator.empty() && !loc) {
        err = who + ": locator \"" + k.locator + "\" is not in the file's locators";
        return false;
    }
    for (const auto& cd : k.copy_defaults) {
        bool known = false;
        if (loc)
            for (const Variant& v : loc->variants) known = known || v.name == cd.first;
        if (!known) {
            err = who + ": copy_defaults names " + cd.first + ", which is not a variant of its locator";
            return false;
        }
    }
    if (k.verified && (k.locator.empty() || k.offset < 0 || !k.has_range() || k.collection.empty() || k.field.empty())) {
        err = who + ": marked verified without collection, field, locator, offset and min / max";
        return false;
    }
    // the owner rule needs FC 27's own value of every copy: a verified knob without it could overwrite another tool's value
    if (k.verified && !k.has_default) {
        err = who + ": marked verified without FC 27's own value (default)";
        return false;
    }
    return true;
}

bool parse_registry(const std::string& text, Registry& out, std::string& err) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "not a JSON object";
        return false;
    }
    if (str_of(j, "schema") != kKnobsSchema) {
        err = std::string("schema is not \"") + kKnobsSchema + "\"";
        return false;
    }
    try {
        Registry r;
        r.build = str_of(j, "build");
        if (r.build.empty()) {
            err = "no build";
            return false;
        }
        r.note = str_of(j, "note");
        r.version = j.value("version", 0);
        if (j.contains("locators") && !j["locators"].is_null()) {
            if (!j["locators"].is_array()) {
                err = "locators must be a list";
                return false;
            }
            for (const auto& o : j["locators"]) {
                Locator l;
                if (!parse_locator(o, l, err)) return false;
                if (r.locator(l.name)) {
                    err = "locator " + l.name + " twice";
                    return false;
                }
                r.locators.push_back(std::move(l));
            }
        }
        if (!j.contains("knobs") || !j["knobs"].is_array()) {
            err = "knobs must be a list";
            return false;
        }
        if (j["knobs"].size() > size_t(kMaxKnobs)) {
            err = "more than " + std::to_string(kMaxKnobs) + " knobs";
            return false;
        }
        for (const auto& o : j["knobs"]) {
            Knob k;
            if (!parse_knob(o, r, k, err)) return false;
            if (r.knob(k.id)) {
                err = "knob " + k.id + " twice";
                return false;
            }
            r.knobs.push_back(std::move(k));
        }
        for (const Knob& k : r.knobs)
            for (const std::string& b : k.below) {
                const Knob* o = r.knob(b);
                if (!o) {
                    err = "knob " + k.id + ": below names knob " + b + ", which is not in the file";
                    return false;
                }
                if (o->type == ValueType::Bool || k.type == ValueType::Bool) {
                    err = "knob " + k.id + ": below cannot compare switches";
                    return false;
                }
            }
        out = std::move(r);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

static ojson num_or_null(bool has, double v) { return has ? ojson(v) : ojson(nullptr); }

std::string registry_json(const Registry& r) {
    ojson j = ojson::object();
    j["schema"] = kKnobsSchema;
    j["build"] = r.build;
    j["version"] = r.version;
    j["note"] = r.note;
    ojson ls = ojson::array();
    for (const Locator& l : r.locators) {
        ojson o = ojson::object();
        o["name"] = l.name;
        o["kind"] = l.kind;
        o["signature"] = l.signature;
        o["pattern"] = l.pattern.pattern;
        o["resolve"] = l.pattern.resolve.empty() ? "none" : l.pattern.resolve;
        o["pattern_offset"] = l.pattern.offset;
        o["expect"] = l.pattern.expect;
        o["deref"] = l.deref;
        ojson ch = ojson::array();
        for (int64_t c : l.chain) ch.push_back((c < 0 ? "-" : "") + hex(uint64_t(c < 0 ? -c : c)));
        o["chain"] = ch;
        o["add"] = l.add;
        ojson cs = ojson::array();
        for (const HeaderCheck& c : l.checks) {
            ojson x = ojson::object();
            x["object"] = c.object;
            x["offset"] = c.offset;
            x["kind"] = c.kind;
            if (!c.signature.empty()) x["signature"] = c.signature;
            if (c.kind.find("_range") != std::string::npos) {
                x["min"] = c.min;
                x["max"] = c.max;
            }
            if (!c.bytes.empty()) x["bytes"] = c.bytes;
            x["note"] = c.note;
            cs.push_back(x);
        }
        o["checks"] = cs;
        if (l.kind == "attrib") {
            o["type"] = l.type;
            o["type_signature"] = hex(l.type_signature);
            o["type_size"] = hex(l.type_size);
            o["type_fields"] = l.type_fields > 0 ? ojson(l.type_fields) : ojson(nullptr);
            ojson vs = ojson::array();
            for (const Variant& v : l.variants) {
                ojson x = {{"name", v.name}, {"instance", v.instance}};
                if (!v.db.empty()) x["db"] = v.db;
                vs.push_back(x);
            }
            o["variants"] = vs;
            if (!l.head.empty()) o["head"] = l.head;
            if (l.head_rva) o["head_rva"] = hex(l.head_rva);
            if (!l.db_type.empty()) o["db_type"] = l.db_type;
            if (l.db_type_signature) o["db_type_signature"] = hex(l.db_type_signature);
            if (!l.group_type.empty()) o["group_type"] = l.group_type;
            if (l.group_type_signature) o["group_type_signature"] = hex(l.group_type_signature);
            if (l.family_slot) o["family_slot"] = hex(l.family_slot);
            if (l.group_slot) o["group_slot"] = hex(l.group_slot);
        }
        o["verified"] = l.verified;
        o["note"] = l.note;
        ls.push_back(o);
    }
    j["locators"] = ls;
    ojson ks = ojson::array();
    for (const Knob& k : r.knobs) {
        ojson o = ojson::object();
        o["id"] = k.id;
        o["label"] = k.label;
        o["group"] = k.group;
        o["collection"] = k.collection;
        o["field"] = k.field;
        o["type"] = type_name(k.type);
        o["unit"] = k.unit;
        o["min"] = num_or_null(k.has_min && k.type != ValueType::Bool, k.min);
        o["max"] = num_or_null(k.has_max && k.type != ValueType::Bool, k.max);
        o["default"] = num_or_null(k.has_default, k.def);
        if (!k.copy_defaults.empty()) {
            ojson cd = ojson::object();
            for (const auto& kv : k.copy_defaults) cd[kv.first] = kv.second;
            o["copy_defaults"] = cd;
        }
        o["step"] = k.step > 0 ? ojson(k.step) : ojson(nullptr);
        o["locator"] = k.locator;
        o["offset"] = k.offset >= 0 ? ojson(hex(uint64_t(k.offset))) : ojson(nullptr);
        o["applies"] = applies_name(k.applies);
        o["when"] = k.when;
        if (!k.below.empty()) o["below"] = k.below;
        o["verified"] = k.verified;
        o["notes"] = k.notes;
        ks.push_back(o);
    }
    j["knobs"] = ks;
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool load_registry(const fs::path& dir, const std::string& build, Registry& out, std::string& err) {
    if (build.empty()) {
        err = "the game build is not known";
        return false;
    }
    for (const std::string& name : knobs_file_names(build)) {
        const fs::path p = dir / name;
        if (!file_exists(p)) continue;
        std::string text;
        if (!read_file(p, text)) {
            err = "cannot read turbo\\tuning\\" + name;
            return false;
        }
        Registry r;
        std::string e;
        if (!parse_registry(text, r, e)) {
            err = "turbo\\tuning\\" + name + " is not used: " + e;
            return false;
        }
        if (r.build != build) {
            err = "turbo\\tuning\\" + name + " is for game build " + r.build + ", the game is " + build;
            return false;
        }
        r.source = p.string();
        out = std::move(r);
        return true;
    }
    std::string names;
    for (const std::string& n : knobs_file_names(build)) names += (names.empty() ? "" : " / ") + n;
    err = "no knobs file for game build " + build + " (turbo\\tuning\\" + names + "): gameplay tuning is off on this build";
    return false;
}

// ------------------------------------------------------------------------------------------------ values
std::string check_value(const Knob& k, double v) {
    if (!std::isfinite(v)) return k.label + ": not a number";
    if (!k.has_range()) return k.label + ": no value range (min / max) in the knobs file: read-only";
    if ((k.type == ValueType::I32 || k.type == ValueType::U8) && v != std::floor(v)) return k.label + ": " + num(v) + " is not a whole number";
    if (k.type == ValueType::Bool && v != 0 && v != 1) return k.label + ": a switch is 0 or 1";
    if (v < k.min || v > k.max) return k.label + ": " + num(v) + " is outside " + num(k.min) + ".." + num(k.max);
    return "";
}

double representable(const Knob& k, double v) {
    switch (k.type) {
        case ValueType::F32: return double(float(v));
        case ValueType::I32:
        case ValueType::U8: return std::round(v);
        case ValueType::Bool: return v >= 0.5 ? 1.0 : 0.0;
    }
    return v;
}

bool same_value(ValueType t, double a, double b) {
    if (t == ValueType::F32) {
        const float fa = float(a), fb = float(b);
        return std::memcmp(&fa, &fb, sizeof(float)) == 0;
    }
    return std::llround(a) == std::llround(b);
}

std::string format_value(const Knob& k, double v, bool with_unit) {
    if (k.type == ValueType::Bool) return v != 0 ? "on" : "off";
    char b[64];
    if (k.type == ValueType::F32) {
        const double step = k.step > 0 ? k.step : 0.001;
        const int dec = step >= 1 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
        std::snprintf(b, sizeof(b), "%.*f", dec, v);
    } else {
        std::snprintf(b, sizeof(b), "%lld", static_cast<long long>(std::llround(v)));
    }
    std::string s = b;
    if (with_unit && !k.unit.empty()) s += " " + k.unit;
    return s;
}

bool read_value(Memory& mem, uint64_t addr, ValueType t, double& out) {
    switch (t) {
        case ValueType::F32: {
            float f = 0;
            if (!mem.rd(addr, f)) return false;
            out = double(f);
            return true;
        }
        case ValueType::I32: {
            int32_t i = 0;
            if (!mem.rd(addr, i)) return false;
            out = double(i);
            return true;
        }
        case ValueType::U8:
        case ValueType::Bool: {
            uint8_t u = 0;
            if (!mem.rd(addr, u)) return false;
            out = double(u);
            return true;
        }
    }
    return false;
}

bool write_value(Memory& mem, uint64_t addr, ValueType t, double v) {
    switch (t) {
        case ValueType::F32: return mem.wr(addr, float(v));
        case ValueType::I32: return mem.wr(addr, int32_t(std::llround(v)));
        case ValueType::U8: return mem.wr(addr, uint8_t(std::llround(v)));
        case ValueType::Bool: return mem.wr(addr, uint8_t(v != 0 ? 1 : 0));
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ reflected type data
static bool in_image(const Lookup& lk, uint64_t p) {
    if (!is_ptr(p)) return false;
    return lk.image_size == 0 || (p >= lk.image_base && p < lk.image_base + lk.image_size);
}

std::string parse_type_info(Memory& mem, uint64_t td, TypeLayout& out) {
    out = TypeLayout();
    const uint64_t name = mem.ptr(td + kTdName, 1);
    if (!name) return "no type name at " + hex(td);
    out.name = mem.read_cstr(name, 160);
    if (out.name.empty()) return "no type name at " + hex(td);
    uint16_t size = 0, count = 0;
    uint32_t sig = 0;
    if (!mem.rd(td + kTdSize, size) || !mem.rd(td + kTdFieldCount, count) || !mem.rd(td + kTdSignature, sig)) return "type " + out.name + ": header not readable";
    if (count > kMaxTypeFields) return "type " + out.name + ": " + std::to_string(count) + " fields";
    out.size = size;
    out.fields = count;
    out.signature = sig;
    if (count == 0) return "";
    const uint64_t fields = mem.ptr(td + kTdFields);
    if (!fields) return "type " + out.name + ": no field table";
    for (int i = 0; i < count; ++i) {
        const uint64_t fi = fields + kFiStride * uint64_t(i);
        const uint64_t fname = mem.ptr(fi + kFiName, 1);
        uint32_t off = 0;
        if (!fname || !mem.rd(fi + kFiOffset, off)) return "type " + out.name + ": field " + std::to_string(i) + " not readable";
        const std::string n = mem.read_cstr(fname, 160);
        if (n.empty()) return "type " + out.name + ": field " + std::to_string(i) + " has no name";
        FieldInfo f;
        f.offset = off;
        // the field type: TypeInfo* -> its TypeInfoData -> its name (display and the type check only)
        const uint64_t ti = mem.ptr(fi + kFiType);
        const uint64_t ftd = ti ? mem.ptr(ti) : 0;
        const uint64_t ftn = ftd ? mem.ptr(ftd + kTdName, 1) : 0;
        if (ftn) f.type = mem.read_cstr(ftn, 80);
        out.field[n] = f;
    }
    return "";
}

bool type_of_field(const std::string& n, ValueType& t) {
    if (n == "Float32" || n == "float") t = ValueType::F32;
    else if (n == "Int32" || n == "int32") t = ValueType::I32;
    else if (n == "Uint8" || n == "UInt8" || n == "uint8") t = ValueType::U8;
    else if (n == "Boolean" || n == "bool") t = ValueType::Bool;
    else return false;
    return true;
}

std::vector<std::pair<size_t, uint64_t>> find_cstrings(const uint8_t* buf, size_t len, uint64_t base, const std::vector<std::string>& names, size_t max_hits) {
    std::vector<std::pair<size_t, uint64_t>> out;
    if (!buf || names.empty()) return out;
    // the first letters (both cases) of every name: a quick filter before the compare
    bool first[256] = {false};
    for (const std::string& n : names)
        if (!n.empty()) {
            first[static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(n[0])))] = true;
            first[static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(n[0])))] = true;
        }
    for (size_t i = 0; i < len && out.size() < max_hits; ++i) {
        if (!first[buf[i]]) continue;
        for (size_t k = 0; k < names.size(); ++k) {
            const std::string& n = names[k];
            if (n.empty() || i + n.size() >= len || buf[i + n.size()] != 0) continue;
            bool eq = true;
            for (size_t j = 0; j < n.size() && eq; ++j)
                eq = std::tolower(static_cast<unsigned char>(buf[i + j])) == std::tolower(static_cast<unsigned char>(n[j]));
            if (eq) out.push_back({k, base + i});
        }
    }
    return out;
}

std::vector<std::pair<uint64_t, uint64_t>> find_qwords(const uint8_t* buf, size_t len, uint64_t base, const std::set<uint64_t>& values, size_t max_hits) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    if (!buf || values.empty()) return out;
    const uint64_t lo = *values.begin(), hi = *values.rbegin();
    size_t start = size_t((8 - (base & 7)) & 7);
    for (size_t i = start; i + 8 <= len && out.size() < max_hits; i += 8) {
        uint64_t v;
        std::memcpy(&v, buf + i, 8);
        if (v < lo || v > hi) continue;
        if (values.count(v)) out.push_back({v, base + i});
    }
    return out;
}

// The TypeInfoData of a type found by its name in FC27.exe (refs: the 8-aligned slots that point at the name in the image), with its
// signature / size / field count when they are given (0 / 0 / -1 = not checked). "" + td + layout, else why
static std::string find_type(Memory& mem, const std::map<std::string, std::vector<uint64_t>>& refs, const std::string& name, uint32_t sig, uint32_t size,
                             int fields, uint64_t& td_out, TypeLayout& layout) {
    std::vector<uint64_t> tds;
    std::string why_not;
    auto it = refs.find(name);
    if (it != refs.end())
        for (uint64_t td : it->second) {
            TypeLayout t;
            const std::string w = parse_type_info(mem, td, t);
            if (!w.empty()) {
                why_not = w;
                continue;
            }
            if (t.name != name) continue;
            if (sig && t.signature != sig) why_not = "type " + name + " has signature " + hex(t.signature) + ", not " + hex(sig);
            else if (size && t.size != size) why_not = "type " + name + " is " + hex(t.size) + " bytes, not " + hex(size);
            else if (fields > 0 && t.fields != fields) why_not = "type " + name + " has " + std::to_string(t.fields) + " fields, not " + std::to_string(fields);
            else {
                if (std::find(tds.begin(), tds.end(), td) == tds.end()) tds.push_back(td);
                layout = t;
            }
        }
    if (tds.empty()) return (why_not.empty() ? "type " + name + " was not found in FC27.exe" : why_not) + ": the layout may have changed on this build";
    if (tds.size() > 1) return "type " + name + " is described " + std::to_string(tds.size()) + " times in FC27.exe";
    td_out = tds.front();
    return "";
}

// An object's head (the live run: +0 the vtable in FC27.exe, +8 its TypeInfo*): +0 must point into FC27.exe (exactly FC27.exe + rva
// when rva is not 0) and, when td is not 0, +8 must be a TypeInfo in FC27.exe whose +0 is td. "" + the vtable, else why
static std::string check_head(Memory& mem, const Lookup& lk, uint64_t obj, uint64_t td, uint64_t rva, uint64_t& vt) {
    vt = 0;
    if (!mem.rd(obj, vt) || !in_image(lk, vt)) return "no vtable at +0";
    if (rva && vt != lk.image_base + rva) return "its vtable is " + hex(vt) + ", not FC27.exe+" + hex(rva);
    if (td) {
        const uint64_t ti = mem.ptr(obj + kInstTypeInfo);
        if (!ti || !in_image(lk, ti) || mem.ptr(ti) != td) return "its TypeInfo at +8 is not the type's";
    }
    return "";
}

// A pointer slot of a database / group runtime object (tag bit 2 masked); 0 = none
static uint64_t slot_ptr(Memory& mem, uint64_t at) {
    uint64_t v = 0;
    if (!mem.rd(at, v)) return 0;
    v &= ~kSlotTag;
    return is_ptr(v, 8) ? v : 0;
}

static bool named(Memory& mem, uint64_t obj, const std::string& name) {
    const uint64_t np = mem.ptr(obj + kInstName, 1);
    return np && !name.empty() && same_name(mem.read_cstr(np, name.size() + 2), name);
}

// L3: the instance a database runtime object points at (+family_slot -> the group object, of group_type when given; +group_slot -> the
// instance). "" + inst, else why
static std::string via_db(Memory& mem, const Locator& l, const Lookup& lk, const Anchor& a, uint64_t db, uint64_t& inst) {
    inst = 0;
    const uint64_t grp = slot_ptr(mem, db + l.family_slot);
    if (!grp) return "the database object " + hex(db) + " holds no group at +" + hex(l.family_slot);
    if (a.group_td) {
        uint64_t vt = 0;
        const std::string w = check_head(mem, lk, grp, a.group_td, 0, vt);
        if (!w.empty()) return "the group object " + hex(grp) + " is not a " + l.group_type + " (" + w + ")";
    }
    inst = slot_ptr(mem, grp + l.group_slot);
    if (!inst) return "the group object " + hex(grp) + " holds no instance at +" + hex(l.group_slot);
    return "";
}

static Anchor resolve_attrib(Memory& mem, const Locator& l, const Lookup& lk) {
    Anchor a;
    if (!lk.refs) {
        a.why = "finding attribulator data needs the game's memory search, which this Turbo does not have here";
        return a;
    }
    // L1: the reflected types by name in one search of FC27.exe: the group's schema with the file's signature, size and field count;
    // for L3 also the database (and group) runtime types
    std::vector<std::string> tnames = {l.type};
    if (l.l3()) {
        tnames.push_back(l.db_type);
        if (!l.group_type.empty()) tnames.push_back(l.group_type);
    }
    const std::map<std::string, std::vector<uint64_t>> tr = lk.refs(tnames, true);
    std::string w = find_type(mem, tr, l.type, l.type_signature, l.type_size, l.type_fields, a.td, a.layout);
    if (w.empty() && l.l3()) {
        TypeLayout other;
        w = find_type(mem, tr, l.db_type, l.db_type_signature, 0, -1, a.db_td, other);
        if (w.empty() && !l.group_type.empty()) w = find_type(mem, tr, l.group_type, l.group_type_signature, 0, -1, a.group_td, other);
    }
    if (!w.empty()) {
        a.why = w;
        return a;
    }
    // L2 / L3: one instance per variant, all with the same vtable at +0 (and the head the locator names)
    std::vector<std::string> names;
    for (const Variant& v : l.variants) names.push_back(v.db.empty() ? v.instance : v.db);
    const std::map<std::string, std::vector<uint64_t>> ir = lk.refs(names, false);
    const uint64_t inst_td = l.head == "typeinfo" ? a.td : 0;
    const std::string head_text = l.head_rva ? "vtable FC27.exe+" + hex(l.head_rva) : std::string("its TypeInfo at +8");
    for (const Variant& v : l.variants) {
        uint64_t inst = 0, db = 0;
        auto it = ir.find(v.db.empty() ? v.instance : v.db);
        if (v.db.empty()) {
            // L2: the instance by its own Name (unique)
            std::vector<uint64_t> found;
            int other_head = 0;
            if (it != ir.end())
                for (uint64_t slot : it->second) {
                    const uint64_t cand = slot - kInstName;
                    uint64_t t0 = 0;
                    if (!mem.rd(cand, t0) || !in_image(lk, t0)) continue;
                    uint64_t vt = 0;
                    if (!check_head(mem, lk, cand, inst_td, l.head_rva, vt).empty()) {
                        ++other_head;
                        continue;
                    }
                    if (std::find(found.begin(), found.end(), cand) == found.end()) found.push_back(cand);
                }
            if (found.size() != 1) {
                a.why = "variant " + v.name + ": " +
                        (!found.empty() ? std::to_string(found.size()) + " instances carry its name"
                         : other_head   ? std::to_string(other_head) + " object(s) carry its name, none with the type's head (" + head_text + ")"
                                        : std::string("its instance is not in memory (the attribulator not loaded yet?)"));
                return a;
            }
            inst = found[0];
        } else {
            // L3: the database runtime object by its Name and its TypeInfo, then its family slot and the group's slot
            std::vector<uint64_t> dbs;
            if (it != ir.end())
                for (uint64_t slot : it->second) {
                    const uint64_t cand = slot - kInstName;
                    uint64_t vt = 0;
                    if (check_head(mem, lk, cand, a.db_td, 0, vt).empty() && std::find(dbs.begin(), dbs.end(), cand) == dbs.end()) dbs.push_back(cand);
                }
            if (dbs.size() != 1) {
                a.why = "variant " + v.name + ": " +
                        (dbs.empty() ? "its database " + v.db + " (" + l.db_type + ") is not in memory (the attribulator not loaded yet?)"
                                     : std::to_string(dbs.size()) + " " + l.db_type + " objects carry the name " + v.db);
                return a;
            }
            db = dbs[0];
            w = via_db(mem, l, lk, a, db, inst);
            if (w.empty()) {
                uint64_t vt = 0;
                w = check_head(mem, lk, inst, inst_td, l.head_rva, vt);
                if (!w.empty()) w = "the instance " + hex(inst) + " its database points at: " + w;
                else if (!named(mem, inst, v.instance)) w = "the instance " + hex(inst) + " its database points at is not named " + v.instance;
            }
            if (!w.empty()) {
                a.why = "variant " + v.name + ": " + w;
                return a;
            }
        }
        uint64_t t0 = 0;
        mem.rd(inst, t0);
        if (a.type_ptr && t0 != a.type_ptr) {
            a.why = "variant " + v.name + "'s instance has another object type (" + hex(t0) + ", not " + hex(a.type_ptr) + ")";
            return a;
        }
        a.type_ptr = t0;
        a.copies.push_back({v.name, inst, db});
    }
    a.addr = l.variants.empty() ? 0 : a.copies.front().base;
    a.ok = true;
    return a;
}

// ------------------------------------------------------------------------------------------------ locate
Anchor resolve_anchor(Memory& mem, const Locator& l, const Lookup& lk) {
    Anchor a;
    const std::string ph = l.placeholder();
    if (!ph.empty()) {
        a.why = ph;
        return a;
    }
    if (l.kind == "attrib") {
        try {
            return resolve_attrib(mem, l, lk);
        } catch (const std::exception& e) {
            a.why = std::string("the attribulator search failed: ") + e.what();
        } catch (...) {
            a.why = "the attribulator search failed";
        }
        return a;
    }
    if (l.kind == "native") {
        a.ok = lk.natives.count(l.name) != 0;
        if (!a.ok) a.why = "native locator " + l.name + " is not in this Turbo";
        return a;
    }
    if (!l.signature.empty()) {
        a.addr = lk.signature ? lk.signature(l.signature) : 0;
        a.ok = a.addr != 0;
        if (!a.ok) a.why = "signature " + l.signature + " was not found on this game build";
        return a;
    }
    if (!lk.scan) {
        a.why = "the inline pattern needs the game's code scan, which this Turbo does not have here";
        return a;
    }
    try {
        const SigResult r = lk.scan(l.pattern);
        if (r.state == SigState::Found && r.address) {
            a.ok = true;
            a.addr = r.address;
        } else {
            a.why = std::string("its pattern is ") + sig_state_name(r.state) + (r.error.empty() ? "" : " (" + r.error + ")") + " in FC27.exe";
        }
    } catch (const std::exception& e) {
        a.why = std::string("the scan failed: ") + e.what();
    } catch (...) {
        a.why = "the scan failed";
    }
    return a;
}

static std::string run_check(Memory& mem, const HeaderCheck& c, uint64_t obj, const Lookup& lk) {
    const uint64_t a = obj + uint64_t(c.offset);
    const std::string at = "[" + hex(obj) + (c.offset >= 0 ? " + " : " - ") + hex(uint64_t(c.offset >= 0 ? c.offset : -c.offset)) + "]";
    if (c.kind == "vtable" || c.kind == "ptr") {
        uint64_t v = 0;
        if (!mem.rd(a, v)) return "header check: " + at + " is not readable";
        if (c.kind == "ptr") return is_ptr(v, 8) ? "" : "header check: " + at + " = " + hex(v) + " is not a pointer";
        const uint64_t want = lk.signature ? lk.signature(c.signature) : 0;
        if (!want) return "header check: signature " + c.signature + " was not found on this game build";
        if (lk.image_size && (want < lk.image_base || want >= lk.image_base + lk.image_size))
            return "header check: signature " + c.signature + " is outside FC27.exe";
        if (v != want) return "header check: " + at + " = " + hex(v) + ", not " + c.signature + " " + hex(want);
        return "";
    }
    if (c.kind == "bytes") {
        std::vector<uint8_t> want, got;
        std::vector<bool> mask;
        if (!parse_pattern(c.bytes, want, mask)) return "header check: bad bytes";
        if (!mem.read_block(a, want.size(), got)) return "header check: " + at + " is not readable";
        for (size_t i = 0; i < want.size(); ++i)
            if (mask[i] && got[i] != want[i]) return "header check: the bytes at " + at + " differ (" + to_hex(got.data(), got.size()) + ")";
        return "";
    }
    double v = 0;
    bool ok = false;
    if (c.kind == "u8_range") {
        uint8_t x = 0;
        ok = mem.rd(a, x);
        v = x;
    } else if (c.kind == "u32_range") {
        uint32_t x = 0;
        ok = mem.rd(a, x);
        v = x;
    } else if (c.kind == "i32_range") {
        int32_t x = 0;
        ok = mem.rd(a, x);
        v = x;
    } else if (c.kind == "f32_range") {
        float x = 0;
        ok = mem.rd(a, x);
        v = x;
        if (ok && !std::isfinite(v)) return "header check: " + at + " is not a finite float";
    } else {
        return "header check: unknown kind " + c.kind;
    }
    if (!ok) return "header check: " + at + " is not readable";
    if (v < c.min || v > c.max) return "header check: " + at + " = " + num(v) + ", outside " + num(c.min) + ".." + num(c.max);
    return "";
}

std::string chase(Memory& mem, const Locator& l, const Anchor& anchor, const Lookup& lk, std::vector<Copy>& bases, std::vector<uint64_t>* objects) {
    bases.clear();
    if (!anchor.ok) return anchor.why.empty() ? "locator " + l.name + " is not resolved" : anchor.why;
    if (l.kind == "attrib") {
        // every copy again: (L3) the database object still the one and still pointing at the instance; the same vtable at +0, its
        // TypeInfo at +8 (head "typeinfo") and its Name at +0x18 (an instance freed and reused fails here)
        for (const Copy& c : anchor.copies) {
            const Variant* v = nullptr;
            for (const Variant& x : l.variants)
                if (x.name == c.variant) v = &x;
            if (c.db) {
                uint64_t vt = 0;
                if (!v || !check_head(mem, lk, c.db, anchor.db_td, 0, vt).empty() || !named(mem, c.db, v->db))
                    return "variant " + c.variant + ": the database object at " + hex(c.db) + " is no longer " + (v ? v->db : std::string("?")) + ": locate again";
                uint64_t inst = 0;
                const std::string w = via_db(mem, l, lk, anchor, c.db, inst);
                if (!w.empty()) return "variant " + c.variant + ": " + w + ": locate again";
                if (inst != c.base)
                    return "variant " + c.variant + ": its database now points at another instance (" + hex(inst) + ", was " + hex(c.base) + "): locate again";
            }
            uint64_t t0 = 0;
            if (!mem.rd(c.base, t0) || t0 != anchor.type_ptr)
                return "variant " + c.variant + ": the instance at " + hex(c.base) + " no longer holds its object type (" + hex(t0) + "): locate again";
            if (l.head == "typeinfo") {
                const uint64_t ti = mem.ptr(c.base + kInstTypeInfo);
                if (!ti || mem.ptr(ti) != anchor.td)
                    return "variant " + c.variant + ": the instance at " + hex(c.base) + " no longer holds its TypeInfo: locate again";
            }
            const uint64_t np = mem.ptr(c.base + kInstName, 1);
            const std::string n = np ? mem.read_cstr(np, v ? v->instance.size() + 2 : 2) : std::string();
            if (!v || !same_name(n, v->instance)) return "variant " + c.variant + ": the instance at " + hex(c.base) + " is no longer named " + (v ? v->instance : "?");
            bases.push_back(c);
        }
        if (bases.empty()) return "locator " + l.name + " found no copy";
        return "";
    }
    if (l.kind == "native") {
        auto it = lk.natives.find(l.name);
        if (it == lk.natives.end() || !it->second) return "native locator " + l.name + " is not in this Turbo";
        uint64_t b = 0;
        std::string why;
        try {
            why = it->second(mem, b);
        } catch (const std::exception& e) {
            why = std::string("native locator threw: ") + e.what();
        } catch (...) {
            why = "native locator threw";
        }
        if (!why.empty()) return why;
        if (!is_ptr(b)) return "native locator " + l.name + " gave no table address";
        bases.push_back({"", b});
        return "";
    }
    std::vector<uint64_t> objs;
    uint64_t cur = anchor.addr;
    if (l.deref) {
        cur = mem.ptr(anchor.addr);
        if (!cur) return "the pointer slot " + hex(anchor.addr) + " holds no object";
    }
    objs.push_back(cur);
    for (size_t i = 0; i < l.chain.size(); ++i) {
        const uint64_t at = cur + uint64_t(l.chain[i]);
        const uint64_t next = mem.ptr(at);
        if (!next) return "chain step " + std::to_string(i + 1) + ": [" + hex(at) + "] holds no pointer";
        cur = next;
        objs.push_back(cur);
    }
    for (const HeaderCheck& c : l.checks) {
        if (c.object < 0 || size_t(c.object) >= objs.size()) return "header check on object " + std::to_string(c.object) + ": no such object";
        const std::string why = run_check(mem, c, objs[size_t(c.object)], lk);
        if (!why.empty()) return why + (c.note.empty() ? "" : " (" + c.note + ")");
    }
    const uint64_t base = cur + uint64_t(l.add);
    if (!is_ptr(base)) return "the table address " + hex(base) + " is not a pointer";
    bases.push_back({"", base});
    if (objects) *objects = objs;
    return "";
}

std::string field_offset(const Knob& k, const Locator& l, const Anchor& a, uint64_t& off) {
    off = 0;
    if (l.kind != "attrib") {
        if (k.offset < 0) return "no field offset";
        off = uint64_t(k.offset);
        return "";
    }
    auto it = a.layout.field.find(k.field);
    if (it == a.layout.field.end()) return "field " + k.field + " is not in type " + l.type + " on this build";
    if (k.offset >= 0 && uint64_t(k.offset) != it->second.offset)
        return "the knobs file puts " + k.field + " at " + hex(uint64_t(k.offset)) + ", the game's type data at " + hex(it->second.offset);
    if (it->second.offset + type_size(k.type) > a.layout.size) return "field " + k.field + " does not fit type " + l.type;
    ValueType ft;
    if (type_of_field(it->second.type, ft) && ft != k.type)
        return "field " + k.field + " is " + it->second.type + " in the game, the knobs file says " + type_name(k.type);
    off = it->second.offset;
    return "";
}

std::string locate_knob(Memory& mem, const Registry& reg, const Knob& k, const AnchorMap& anchors, const Lookup& lk, KnobAt& out) {
    out = KnobAt();
    auto no = [](const std::string& why) { return "not located on this build (" + why + ")"; };
    std::string ph = k.placeholder();
    if (!ph.empty()) return no(ph);
    const Locator* l = reg.locator(k.locator);
    if (!l) return no("locator " + k.locator + " is not in the knobs file");
    ph = l->placeholder();
    if (!ph.empty()) return no(ph);
    auto it = anchors.find(l->name);
    if (it == anchors.end()) return no("locator " + l->name + " is not resolved yet");
    if (it->second.pending) return no("locator " + l->name + ": " + (it->second.why.empty() ? std::string("searching ...") : it->second.why));
    std::vector<Copy> bases;
    std::string why = chase(mem, *l, it->second, lk, bases);
    if (!why.empty()) return no(why);
    uint64_t off = 0;
    why = field_offset(k, *l, it->second, off);
    if (!why.empty()) return no(why);
    for (const Copy& c : bases) {
        const uint64_t addr = c.base + off;
        const std::string which = c.variant.empty() ? std::string() : " (" + c.variant + ")";
        double v = 0;
        if (!read_value(mem, addr, k.type, v)) return no("the value at " + hex(addr) + which + " is not readable");
        if (k.has_range() && (v < k.min || v > k.max || !std::isfinite(v)))
            return no("the value at " + hex(addr) + which + " reads " + num(v) + ", outside Turbo's range " + num(k.min) + ".." + num(k.max) +
                      ": another tool changed it, or the layout differs on this build");
        out.copies.push_back({c.variant, addr, v});
    }
    out.addr = out.copies.front().addr;
    out.value = out.copies.front().value;
    return "";
}

// ------------------------------------------------------------------------------------------------ gates
std::string Switches::blocking() const {
    if (master) return master_switch_file();
    if (off) return kill_switch_file();
    if (attrib) return attrib_switch_file();
    return "";
}

Switches read_switches(const fs::path& out) {
    Switches s;
    s.opt_in = file_exists(out / opt_in_file());
    s.off = file_exists(out / kill_switch_file());
    s.master = file_exists(out / master_switch_file());
    s.attrib = file_exists(out / attrib_switch_file());
    return s;
}

bool Gates::pass() const {
    for (const Gate& g : list)
        if (!g.pass()) return false;
    return true;
}

std::string Gates::first() const {
    for (const Gate& g : list)
        if (!g.pass()) return g.name + ": " + g.why;
    return "";
}

static std::string when_text(const std::vector<std::string>& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) s += (i ? (i + 1 == w.size() ? " or " : ", ") : "") + std::string(w[i] == "hub" ? "the career hub" : w[i]);
    return s;
}

Gates evaluate(const GateInput& in) {
    Gates gs;
    {
        Gate g{"G1 switches", ""};
        const std::string block = in.sw.blocking();
        if (!in.restore && !block.empty()) g.why = "kill switch turbo_output\\" + block + " is present";
        else if (!in.restore && !in.sw.opt_in) g.why = std::string("off until turbo_output\\") + opt_in_file() + " exists (opt-in)";
        gs.list.push_back(g);
    }
    {
        Gate g{"G2 career phase", ""};
        if (!in.bridge) g.why = "Turbo's Lua side has not reported the career yet";
        else if (!in.in_cm) g.why = "not in a career";
        else if (in.match_phase.empty()) g.why = "this Lua side does not report the match phase: update Turbo's Lua files";
        else if (in.match_phase == "unknown")
            g.why = "the match phase is not known yet (no hub, match or day event seen since Turbo started): advance a day or reload the career";
        else {
            if (in.knobs.empty()) {
                if (in.match_phase != "hub") g.why = "a match is being set up or played (Lua: " + in.match_phase + "): apply in the career hub";
            } else {
                for (const Knob* k : in.knobs)
                    if (k && !k->allows(in.match_phase)) {
                        g.why = k->label + " may be written in " + when_text(k->when) + " only (Lua: " + in.match_phase + ")";
                        break;
                    }
            }
            if (g.why.empty() && in.save_age >= 0 && in.save_age < kSaveQuietSeconds)
                g.why = "a save started " + std::to_string(in.save_age) + " s ago: wait a moment";
            else if (g.why.empty() && in.load_gen < 0)
                g.why = "this Lua side does not report career loads: update Turbo's Lua files";
        }
        gs.list.push_back(g);
    }
    gs.list.push_back({"G3 knobs file", in.registry});
    gs.list.push_back({"G4 located", in.locate});
    gs.list.push_back({"G5 values", in.values});
    gs.list.push_back({"session", in.restore ? std::string() : in.session_off});
    return gs;
}

// ------------------------------------------------------------------------------------------------ plans
static void sort_by_registry(const Registry& reg, std::vector<Write>& ws) {
    auto index = [&](const std::string& id) {
        for (size_t i = 0; i < reg.knobs.size(); ++i)
            if (reg.knobs[i].id == id) return i;
        return reg.knobs.size();
    };
    std::stable_sort(ws.begin(), ws.end(), [&](const Write& a, const Write& b) { return index(a.id) < index(b.id); });
}

// The copy's label in lines ("" for the only copy of a chain / native locator)
static std::string copy_label(const std::string& copy) { return copy.empty() ? std::string() : " [" + copy + "]"; }

void split_copy_key(const std::string& key, std::string& id, std::string& copy) {
    const size_t at = key.find('@');
    if (at == std::string::npos) {
        id = key;
        copy.clear();
    } else {
        id = key.substr(0, at);
        copy = key.substr(at + 1);
    }
}

namespace {
// The wanted values of one knob: one value for every copy, or one per copy
struct Want {
    bool all = false;
    double v = 0;
    std::map<std::string, double> per;
};
std::map<std::string, Want> wants_by_knob(const std::map<std::string, double>& wanted) {
    std::map<std::string, Want> by;
    for (const auto& kv : wanted) {
        std::string id, copy;
        split_copy_key(kv.first, id, copy);
        Want& w = by[id];
        if (kv.first.find('@') == std::string::npos) {
            w.all = true;
            w.v = kv.second;
        } else {
            w.per[copy] = kv.second;
        }
    }
    return by;
}
}  // namespace

// The value of one copy of a knob after a plan: the wanted one, else its value now (the same variant, else the first copy)
static bool value_after(const std::map<std::string, KnobLive>& live, const std::map<std::string, Want>& by, const std::string& id,
                        const std::string& copy, double& v) {
    auto w = by.find(id);
    if (w != by.end()) {
        if (w->second.all) {
            v = w->second.v;
            return true;
        }
        auto p = w->second.per.find(copy);
        if (p != w->second.per.end()) {
            v = p->second;
            return true;
        }
    }
    auto l = live.find(id);
    if (l == live.end() || !l->second.located || l->second.copies.empty()) return false;
    const CopyAt* c = l->second.copy(copy);
    v = c ? c->value : l->second.copies.front().value;
    return true;
}

std::string make_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& wanted, PlanKind kind,
                      Plan& out) {
    out = Plan();
    out.kind = kind;
    const std::map<std::string, Want> by = wants_by_knob(wanted);
    for (const auto& kb : by) {
        const Knob* k = reg.knob(kb.first);
        if (!k) return "knob " + kb.first + " is not in the knobs file";
        const Want& w = kb.second;
        if (w.all && !w.per.empty()) return k->label + ": give one value for every copy or one value per copy, not both";
        auto it = live.find(kb.first);
        if (it == live.end() || !it->second.located)
            return k->label + ": " + (it == live.end() || it->second.why.empty() ? std::string("not located on this build") : it->second.why);
        if (!it->second.writable) return k->label + ": no value range (min / max) in the knobs file: read-only";
        if (it->second.copies.empty()) return k->label + ": not located on this build";
        for (const auto& p : w.per)
            if (!it->second.copy(p.first)) return k->label + ": there is no copy \"" + p.first + "\" on this build";
        // every copy's target: the knob is written into EVERY copy or into none
        std::vector<Write> ws;
        bool changes = false;
        for (const CopyAt& c : it->second.copies) {
            double v = w.v;
            if (!w.all) {
                auto p = w.per.find(c.variant);
                if (p == w.per.end()) return k->label + ": no value for its " + c.variant + " copy (every copy of a knob is written together)";
                v = p->second;
            }
            const std::string why = check_value(*k, v);
            if (!why.empty()) return why;
            v = representable(*k, v);
            changes = changes || !same_value(k->type, c.value, v);
            ws.push_back({k->id, c.value, v, c.variant});
        }
        if (changes) out.writes.insert(out.writes.end(), ws.begin(), ws.end());
    }
    // the knobs that must stay below others (the line height bands): checked with the values after the plan, copy by copy
    for (const Knob& k : reg.knobs)
        for (const std::string& b : k.below) {
            if (!by.count(k.id) && !by.count(b)) continue;
            const Knob* o = reg.knob(b);
            auto lk = live.find(k.id);
            if (!o || lk == live.end() || !lk->second.located) continue;
            std::vector<std::string> copies;
            for (const CopyAt& c : lk->second.copies) copies.push_back(c.variant);
            if (copies.empty()) copies.push_back("");
            for (const std::string& cp : copies) {
                double a = 0, c = 0;
                if (!value_after(live, by, k.id, cp, a) || !value_after(live, by, b, cp, c)) continue;
                if (!(representable(k, a) < representable(*o, c)))
                    return k.label + copy_label(copies.size() > 1 ? cp : std::string()) + " (" + format_value(k, a) + ") must stay below " + o->label + " (" +
                           format_value(*o, c) + ")";
            }
        }
    sort_by_registry(reg, out.writes);
    return "";
}

std::string describe_writes(const Registry& reg, const std::vector<Write>& ws) {
    std::string s;
    for (const Write& w : ws) {
        const Knob* k = reg.knob(w.id);
        s += (s.empty() ? "" : ", ") + (k ? k->label + copy_label(w.copy) + " " + format_value(*k, w.old_value) + " -> " + format_value(*k, w.new_value)
                                          : w.id + copy_label(w.copy) + " " + num(w.old_value) + " -> " + num(w.new_value));
    }
    return s.empty() ? "nothing" : s;
}

// ------------------------------------------------------------------------------------------------ the job
ApplyResult apply_plan(Memory& game, const Registry& reg, const AnchorMap& anchors, const Lookup& lk, const Plan& plan, const JobOptions& opt) {
    ApplyResult r;
    r.kind = plan.kind;
    Counting mem(game);
    const std::string what = std::string("gameplay tuning ") + plan_kind_name(plan.kind);
    auto fail = [&](const char* stage, const std::string& msg) {
        r.ok = false;
        r.stage = stage;
        r.message = msg;
        r.log_line = what + " refused (" + stage + "): " + msg;
        r.reads = mem.reads;
        return r;
    };
    try {
        if (plan.writes.empty()) {
            r.ok = true;
            r.stage = "done";
            r.message = "nothing to change";
            r.log_line = what + ": nothing to change";
            return r;
        }
        struct Item {
            const Knob* k;
            std::string copy;
            uint64_t addr;
            double snap;
            double want;
        };
        std::vector<Item> items;
        std::set<std::string> seen;
        std::map<std::string, std::set<std::string>> located_copies, planned_copies;  // by knob: the copies located now, the copies the plan writes
        for (const Write& w : plan.writes) {
            const Knob* k = reg.knob(w.id);
            if (!k) return fail("registry", "knob " + w.id + " is not in the knobs file: nothing written");
            const std::string label = k->label + copy_label(w.copy);
            if (!seen.insert(copy_key(w.id, w.copy)).second) return fail("plan", "knob " + copy_key(w.id, w.copy) + " twice in one plan: nothing written");
            // 1. the knob located again (the tables can move between frames), the copy of the write
            KnobAt at;
            std::string why = locate_knob(mem, reg, *k, anchors, lk, at);
            if (!why.empty()) return fail("locate", k->label + ": " + why + ": nothing written");
            const CopyAt* c = nullptr;
            for (const CopyAt& x : at.copies)
                if (x.variant == w.copy) c = &x;
            if (!c) return fail("locate", label + ": no such copy on this build: nothing written");
            // 2. the value (G5 again) and the value Apply was pressed for
            why = check_value(*k, w.new_value);
            if (!why.empty()) return fail("G5", why + ": nothing written");
            if (!same_value(k->type, c->value, w.old_value))
                return fail("changed", label + " is " + format_value(*k, c->value) + ", not " + format_value(*k, w.old_value) +
                                           " as when Apply was pressed: nothing written");
            // 3. who owns the value: FC 27's own value of this copy (Knob::game_value) or Turbo's last write, nothing else (another tool
            //    or mod changed it). A copy whose FC 27 value the knobs file does not give is only written over Turbo's own last write
            if (opt.owner_check) {
                double fc = 0;
                const bool known = k->game_value(w.copy, fc);
                bool turbo = false;
                if (opt.last_written) {
                    auto lw = opt.last_written->find(copy_key(w.id, w.copy));
                    turbo = lw != opt.last_written->end() && same_value(k->type, c->value, lw->second);
                }
                const bool vanilla = known && same_value(k->type, c->value, representable(*k, fc));
                if (!vanilla && !turbo) {
                    if (!known)
                        return fail("owner", label + " is " + format_value(*k, c->value) +
                                                 ": FC 27's own value of it is not in the knobs file, so Turbo cannot tell whether another tool or mod "
                                                 "changed it; nothing written");
                    return fail("owner", label + " is " + format_value(*k, c->value) + ", neither FC 27's " + format_value(*k, representable(*k, fc)) +
                                             " nor Turbo's last write: another tool or mod changed it; nothing written");
                }
            }
            if (!located_copies.count(w.id))
                for (const CopyAt& x : at.copies) located_copies[w.id].insert(x.variant);
            planned_copies[w.id].insert(w.copy);
            for (const Item& o : items)
                if (c->addr < o.addr + type_size(o.k->type) && o.addr < c->addr + type_size(k->type))
                    return fail("plan", label + " and " + o.k->label + copy_label(o.copy) + " overlap at " + hex(c->addr) +
                                            ": the knobs file is wrong; nothing written");
            items.push_back({k, w.copy, c->addr, c->value, representable(*k, w.new_value)});
        }
        // copies go together: a knob is written into every copy located now (base and simulation) or the job writes nothing
        for (const auto& kv : planned_copies)
            if (kv.second != located_copies[kv.first]) {
                const Knob* k = reg.knob(kv.first);
                return fail("plan", (k ? k->label : kv.first) + ": the plan writes " + std::to_string(kv.second.size()) + " of its " +
                                        std::to_string(located_copies[kv.first].size()) +
                                        " copies; every copy of a knob is written together: nothing written");
            }
        // 4. the snapshot (what every copy holds now)
        for (const Item& it : items) r.snapshot[copy_key(it.k->id, it.copy)] = it.snap;
        if (opt.cancel && opt.cancel->load()) return fail("cancelled", "the job was given up: nothing written");
        // 5. write every copy of every knob
        bool write_failed = false;
        for (const Item& it : items)
            if (!write_value(mem, it.addr, it.k->type, it.want)) write_failed = true;
        // 6. read every one back
        std::string bad;
        for (const Item& it : items) {
            double back = 0;
            if (!read_value(mem, it.addr, it.k->type, back) || !same_value(it.k->type, back, it.want)) {
                bad = it.k->label + copy_label(it.copy) + " reads " + format_value(*it.k, back) + ", not " + format_value(*it.k, it.want);
                break;
            }
        }
        if (write_failed && bad.empty()) bad = "a write was refused by the memory layer";
        r.writes.clear();
        for (const Item& it : items) r.writes.push_back({it.k->id, it.snap, it.want, it.copy});
        if (!bad.empty()) {
            // every copy back from the snapshot, then read back
            for (const Item& it : items) {
                double cur = 0;
                if (!read_value(mem, it.addr, it.k->type, cur) || !same_value(it.k->type, cur, it.snap)) write_value(mem, it.addr, it.k->type, it.snap);
            }
            bool restored = true;
            for (const Item& it : items) {
                double back = 0;
                if (!read_value(mem, it.addr, it.k->type, back) || !same_value(it.k->type, back, it.snap)) restored = false;
            }
            r.undone = restored;
            r.restore_failed = !restored;
            r.session_off = true;
            r.ok = false;
            r.stage = "readback";
            r.message = "read-back mismatch (" + bad + "): " + (restored ? "every knob was put back from the snapshot" : "PUTTING BACK FAILED") +
                        "; gameplay tuning is off for this session";
            r.log_line = what + ": " + describe_writes(reg, r.writes) + " READ-BACK MISMATCH (" + bad + "), " +
                         (restored ? "undone from the snapshot" : "undo FAILED");
            r.reads = mem.reads;
            return r;
        }
        r.ok = true;
        r.written = true;
        r.stage = "done";
        r.message = "written and read back";
        r.log_line = what + ": " + describe_writes(reg, r.writes) + " (read back ok)";
        r.reads = mem.reads;
        return r;
    } catch (const std::exception& e) {
        return fail("exception", e.what());
    } catch (...) {
        return fail("exception", "unknown error");
    }
}

// ------------------------------------------------------------------------------------------------ the undo record
static ojson writes_json(const std::vector<Write>& ws) {
    ojson a = ojson::array();
    for (const Write& w : ws) {
        ojson x = {{"id", w.id}, {"old", w.old_value}, {"new", w.new_value}};
        if (!w.copy.empty()) x["copy"] = w.copy;
        a.push_back(x);
    }
    return a;
}

static bool writes_from(const json& a, std::vector<Write>& out, std::string* err) {
    out.clear();
    if (!a.is_array()) {
        if (err) *err = "knobs is not a list";
        return false;
    }
    for (const auto& x : a) {
        if (!x.is_object() || !x.contains("id") || !x["id"].is_string() || !x.contains("old") || !x["old"].is_number() || !x.contains("new") ||
            !x["new"].is_number()) {
            if (err) *err = "a knob entry needs id, old and new";
            return false;
        }
        out.push_back({x["id"].get<std::string>(), x["old"].get<double>(), x["new"].get<double>(), x.value("copy", std::string())});
    }
    return true;
}

std::string UndoRecord::to_json() const {
    ojson j = ojson::object();
    j["turbo_tuning_undo"] = 1;
    j["build"] = build;
    j["when"] = when;
    j["session"] = session;
    j["load_gen"] = load_gen;
    j["state"] = state;
    j["knobs"] = writes_json(knobs);
    j["note"] = note;
    return j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool UndoRecord::parse(const std::string& text, UndoRecord& out, std::string* err) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_tuning_undo")) {
        if (err) *err = "not a gameplay tuning undo file";
        return false;
    }
    try {
        UndoRecord u;
        u.build = j.value("build", std::string());
        u.when = j.value("when", std::string());
        u.session = j.value("session", std::string());
        u.load_gen = j.value("load_gen", -1LL);
        u.state = j.value("state", std::string());
        u.note = j.value("note", std::string());
        if (!writes_from(j.value("knobs", json::array()), u.knobs, err)) return false;
        out = std::move(u);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("undo file: ") + e.what();
        return false;
    }
}

bool load_undo(const fs::path& p, UndoRecord& out, std::string* err) {
    std::string text;
    if (!read_file(p, text)) {
        if (err) *err = "no " + p.filename().string();
        return false;
    }
    return UndoRecord::parse(text, out, err);
}

bool save_undo(const fs::path& p, const UndoRecord& u, std::string* err) { return write_file(p, u.to_json(), err); }

std::string same_career(const UndoRecord& u, const std::string& session, long long load_gen) {
    if (u.session != session || u.load_gen != load_gen)
        return "the undo file is from another career load (written in load " + std::to_string(u.load_gen) + (u.session == session ? "" : " of another game session") +
               ", this is load " + std::to_string(load_gen) + "): a career load may have rebuilt the tables; use Restore game defaults";
    return "";
}

UndoRecord merge_undo(const UndoRecord& prev, const UndoRecord& next) {
    if (prev.session != next.session || prev.load_gen != next.load_gen || (prev.state != "written" && prev.state != "failed")) return next;
    UndoRecord out = next;
    for (const Write& p : prev.knobs) {
        auto it = std::find_if(out.knobs.begin(), out.knobs.end(), [&](const Write& w) { return w.id == p.id && w.copy == p.copy; });
        if (it == out.knobs.end()) out.knobs.push_back(p);                   // only the earlier Apply touched it: it stays listed
        else if (it->old_value == p.new_value) it->old_value = p.old_value;  // written again over Turbo's value: the first old value
    }
    return out;
}

static const CopyAt* live_copy(const std::map<std::string, KnobLive>& live, const std::string& id, const std::string& copy, std::string& why) {
    auto it = live.find(id);
    if (it == live.end() || !it->second.located) {
        why = it == live.end() ? std::string("not located") : it->second.why;
        return nullptr;
    }
    const CopyAt* c = it->second.copy(copy);
    if (!c) why = "no copy " + copy + " on this build";
    return c;
}

std::string make_undo_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const UndoRecord& u, const std::string& session,
                           long long load_gen, Plan& out, std::vector<std::string>& skipped) {
    out = Plan();
    out.kind = PlanKind::Undo;
    skipped.clear();
    if (u.state != "written" && u.state != "failed") return "nothing to put back (the undo file says \"" + u.state + "\")";
    const std::string sc = same_career(u, session, load_gen);
    if (!sc.empty()) return sc;
    // by knob, in the record's order: every copy of a knob goes back together, or none of them
    std::vector<std::string> order;
    std::map<std::string, std::vector<const Write*>> by;
    for (const Write& w : u.knobs) {
        if (!by.count(w.id)) order.push_back(w.id);
        by[w.id].push_back(&w);
    }
    for (const std::string& id : order) {
        const Knob* k = reg.knob(id);
        if (!k) {
            skipped.push_back(id + " is not in the knobs file");
            continue;
        }
        std::vector<Write> ws;
        std::string skip;
        for (const Write* w : by[id]) {
            const std::string label = k->label + copy_label(w->copy);
            std::string why;
            const CopyAt* c = live_copy(live, id, w->copy, why);
            if (!c) {
                skip = label + ": " + why;
                break;
            }
            if (!same_value(k->type, c->value, w->new_value)) {
                skip = label + " is " + format_value(*k, c->value) + ", not Turbo's " + format_value(*k, w->new_value);
                break;
            }
            why = check_value(*k, w->old_value);
            if (!why.empty()) {
                skip = why;
                break;
            }
            ws.push_back({id, c->value, representable(*k, w->old_value), w->copy});
        }
        auto l = live.find(id);
        if (skip.empty() && l != live.end() && ws.size() != l->second.copies.size())
            skip = k->label + ": the undo file holds " + std::to_string(ws.size()) + " of its " + std::to_string(l->second.copies.size()) +
                   " copies (every copy goes back together)";
        if (!skip.empty()) {
            skipped.push_back(skip);
            continue;
        }
        out.writes.insert(out.writes.end(), ws.begin(), ws.end());
    }
    sort_by_registry(reg, out.writes);
    if (out.writes.empty()) {
        std::string s;
        for (const auto& x : skipped) s += (s.empty() ? "" : "; ") + x;
        return "nothing can be put back" + (s.empty() ? std::string() : ": " + s);
    }
    return "";
}

std::string make_restore_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const std::map<std::string, double>& defaults, Plan& out,
                              std::vector<std::string>& skipped, const std::string& only) {
    out = Plan();
    out.kind = PlanKind::Restore;
    skipped.clear();
    if (defaults.empty()) return "nothing to restore: Turbo has not written a knob in this game session";
    std::set<std::string> used;
    for (const Knob& k : reg.knobs) {
        // the copies the defaults hold for this knob (by copy_key)
        std::map<std::string, double> mine;
        for (const auto& kv : defaults) {
            std::string id, copy;
            split_copy_key(kv.first, id, copy);
            if (id != k.id) continue;
            used.insert(kv.first);
            mine[copy] = kv.second;
        }
        if (mine.empty() || (!only.empty() && k.id != only)) continue;
        auto it = live.find(k.id);
        if (it == live.end() || !it->second.located) {
            skipped.push_back(k.label + ": " + (it == live.end() ? std::string("not located") : it->second.why));
            continue;
        }
        // every copy located now needs its default (copies go back together)
        std::vector<Write> ws;
        std::string skip;
        bool changes = false;
        for (const CopyAt& c : it->second.copies) {
            auto d = mine.find(c.variant);
            if (d == mine.end()) {
                skip = k.label + copy_label(c.variant) + ": no game default recorded for this copy";
                break;
            }
            const std::string why = check_value(k, d->second);
            if (!why.empty()) {
                skip = why;
                break;
            }
            changes = changes || !same_value(k.type, c.value, d->second);
            ws.push_back({k.id, c.value, representable(k, d->second), c.variant});
        }
        for (const auto& m : mine)
            if (skip.empty() && !it->second.copy(m.first)) skip = k.label + copy_label(m.first) + ": no such copy on this build";
        if (!skip.empty()) {
            skipped.push_back(skip);
            continue;
        }
        if (changes) out.writes.insert(out.writes.end(), ws.begin(), ws.end());
    }
    if (only.empty())
        for (const auto& kv : defaults)
            if (!used.count(kv.first)) skipped.push_back(kv.first + " is not in the knobs file");
    sort_by_registry(reg, out.writes);
    return "";
}

// ------------------------------------------------------------------------------------------------ kept values
std::string KeepStore::to_json() const {
    ojson j = ojson::object();
    j["turbo_tuning_keep"] = 1;
    j["on"] = on;
    j["build"] = build;
    j["when"] = when;
    ojson v = ojson::object();
    for (const auto& kv : values) v[kv.first] = kv.second;
    j["values"] = v;
    return j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool KeepStore::parse(const std::string& text, KeepStore& out, std::string* err) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_tuning_keep")) {
        if (err) *err = "not a gameplay tuning keep file";
        return false;
    }
    KeepStore k;
    k.on = j.value("on", false);
    k.build = j.value("build", std::string());
    k.when = j.value("when", std::string());
    if (j.contains("values")) {
        if (!j["values"].is_object()) {
            if (err) *err = "values is not an object";
            return false;
        }
        for (auto it = j["values"].begin(); it != j["values"].end(); ++it) {
            if (!it.value().is_number() || !std::isfinite(it.value().get<double>())) {
                if (err) *err = "value of " + it.key() + " is not a number";
                return false;
            }
            k.values[it.key()] = it.value().get<double>();
        }
    }
    out = std::move(k);
    return true;
}

bool load_keep(const fs::path& p, KeepStore& out, std::string* err) {
    std::string text;
    if (!read_file(p, text)) {
        if (err) *err = "";
        return false;
    }
    return KeepStore::parse(text, out, err);
}

bool save_keep(const fs::path& p, const KeepStore& k, std::string* err) { return write_file(p, k.to_json(), err); }

std::string make_keep_plan(const Registry& reg, const std::map<std::string, KnobLive>& live, const KeepStore& k, Plan& out,
                           std::vector<std::string>& skipped) {
    out = Plan();
    out.kind = PlanKind::Reapply;
    skipped.clear();
    if (!k.on) return "keeping is off";
    if (!k.build.empty() && k.build != reg.build) return "the kept values are for game build " + k.build + ", the game is " + reg.build;
    for (const auto& kv : k.values) {
        const Knob* kn = reg.knob(kv.first);
        if (!kn) {
            skipped.push_back(kv.first + " is not in the knobs file");
            continue;
        }
        auto it = live.find(kv.first);
        if (it == live.end() || !it->second.located) {
            skipped.push_back(kn->label + ": " + (it == live.end() ? std::string("not located") : it->second.why));
            continue;
        }
        const std::string why = it->second.writable ? check_value(*kn, kv.second) : kn->label + ": read-only";
        if (!why.empty()) {
            skipped.push_back(why);
            continue;
        }
        // every copy, or none (copies go together)
        const double v = representable(*kn, kv.second);
        bool changes = false;
        for (const CopyAt& c : it->second.copies) changes = changes || !same_value(kn->type, c.value, v);
        if (changes)
            for (const CopyAt& c : it->second.copies) out.writes.push_back({kv.first, c.value, v, c.variant});
    }
    sort_by_registry(reg, out.writes);
    return "";
}

// ------------------------------------------------------------------------------------------------ presets
std::string Preset::to_json() const {
    ojson j = ojson::object();
    j["turbo_tuning_preset"] = 1;
    j["id"] = id;
    j["name"] = name;
    j["author"] = author;
    j["note"] = note;
    ojson cs = ojson::array();
    for (const PresetChange& c : changes) {
        ojson x = ojson::object();
        x["knob"] = c.knob;
        if (c.relative) x["relative_pct"] = c.pct;
        else x["value"] = c.value;
        cs.push_back(x);
    }
    j["changes"] = cs;
    return j.dump(1, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool Preset::parse(const std::string& text, Preset& out, std::string* err) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("turbo_tuning_preset")) {
        if (err) *err = "not a Turbo gameplay tuning preset";
        return false;
    }
    Preset p;
    p.id = j.value("id", std::string());
    p.name = j.value("name", std::string());
    p.author = j.value("author", std::string());
    p.note = j.value("note", std::string());
    if (p.name.empty()) {
        if (err) *err = "the preset has no name";
        return false;
    }
    if (p.id.empty()) p.id = "user_" + safe_name(p.name);
    if (!j.contains("changes") || !j["changes"].is_array()) {
        if (err) *err = "changes is not a list";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& c : j["changes"]) {
        PresetChange pc;
        if (!c.is_object() || !c.contains("knob") || !c["knob"].is_string()) {
            if (err) *err = "a change needs a knob";
            return false;
        }
        pc.knob = c["knob"].get<std::string>();
        if (!seen.insert(pc.knob).second) {
            if (err) *err = "knob " + pc.knob + " twice";
            return false;
        }
        const bool rel = c.contains("relative_pct"), abs = c.contains("value");
        if (rel == abs) {
            if (err) *err = pc.knob + ": give relative_pct or value (one of them)";
            return false;
        }
        const json& v = rel ? c["relative_pct"] : c["value"];
        if (!v.is_number() || !std::isfinite(v.get<double>())) {
            if (err) *err = pc.knob + ": not a number";
            return false;
        }
        pc.relative = rel;
        if (rel) {
            pc.pct = v.get<double>();
            if (pc.pct < -100.0 || pc.pct > 1000.0) {
                if (err) *err = pc.knob + ": relative_pct must be -100..1000";
                return false;
            }
        } else {
            pc.value = v.get<double>();
        }
        p.changes.push_back(pc);
    }
    out = std::move(p);
    return true;
}

const std::vector<Preset>& builtin_presets() {
    // Turbo's own presets, relative changes from the game's values (the knob ids of the knobs file). They name a direction and a size,
    // not a value table (no gameplay mod's values): once the live locator confirms a knob, its change lands on the real field.
    static const std::vector<Preset> p = [] {
        std::vector<Preset> v;
        Preset a;
        a.id = "turbo_high_line_press";
        a.name = "High line + aggressive press";
        a.author = "Turbo";
        a.note = "Turbo's own: the High band starts at a lower defensive depth, the press and zone marking start earlier, markers stay closer, "
                 "the team defends shorter. Relative to the game's values.";
        a.changes = {{"lineband.balanced_max", true, -15, 0}, {"lineband.high_min", true, -15, 0}, {"pressing.trigger_depth", true, -40, 0},
                     {"pressing.zone_marking_depth", true, -30, 0}, {"marking.tightness", true, 20, 0}, {"shape.length_out", true, -10, 0},
                     {"shape.gap_dmf", true, -15, 0}};
        v.push_back(a);
        Preset b;
        b.id = "turbo_deep_block";
        b.name = "Deep compact block";
        b.author = "Turbo";
        b.note = "Turbo's own: the Deep band reaches a higher defensive depth, the block is narrower and shorter with smaller gaps between the "
                 "lines, the press starts later. Relative to the game's values.";
        b.changes = {{"lineband.deep_max", true, 33, 0}, {"lineband.balanced_min", true, 32, 0}, {"shape.width_out", true, -10, 0},
                     {"shape.length_out", true, -15, 0}, {"shape.gap_dmf", true, -15, 0}, {"shape.gap_mf_deep", true, -15, 0},
                     {"pressing.trigger_depth", true, 30, 0}};
        v.push_back(b);
        return v;
    }();
    return p;
}

std::string preset_file_name(const Preset& p) {
    if (p.turbo() && p.id.rfind("turbo_", 0) == 0) return safe_name(p.id) + ".json";
    return safe_name(p.name) + ".json";
}

int ensure_builtin_presets(const fs::path& dir, std::string* err) {
    int n = 0;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const Preset& p : builtin_presets()) {
        const fs::path f = dir / preset_file_name(p);
        const std::string want = p.to_json();
        std::string have;
        if (read_file(f, have)) {
            if (have == want) continue;
            Preset on_disk;
            // the file is Turbo's (its author): a newer Turbo replaces it; a user's file of the same name is never touched
            if (!Preset::parse(have, on_disk, nullptr) || !on_disk.turbo()) continue;
        }
        if (write_file(f, want, err)) ++n;
    }
    return n;
}

std::vector<Preset> load_presets(const fs::path& dir, std::vector<std::string>* errors) {
    std::vector<Preset> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".json") files.push_back(e.path());
        if (files.size() >= 500) break;
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        std::string text, err;
        Preset p;
        if (!read_file(f, text) || !Preset::parse(text, p, &err)) {
            if (errors) errors->push_back(f.filename().string() + ": " + (err.empty() ? "cannot read" : err));
            continue;
        }
        p.file = f;
        out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) {
        if (a.turbo() != b.turbo()) return a.turbo();
        return a.name < b.name;
    });
    return out;
}

PresetValues preset_values(const Preset& p, const Registry& reg, const std::function<bool(const std::string&, double&)>& base_of) {
    PresetValues out;
    for (const PresetChange& c : p.changes) {
        const Knob* k = reg.knob(c.knob);
        if (!k) {
            out.skipped.push_back(c.knob + ": not in the knobs file");
            continue;
        }
        if (!k->has_range()) {
            out.skipped.push_back(k->label + ": no value range yet");
            continue;
        }
        double v = 0;
        if (c.relative) {
            if (k->type == ValueType::Bool) {
                out.skipped.push_back(k->label + " is a switch: a relative change does not apply");
                continue;
            }
            double base = 0;
            if (!base_of || !base_of(c.knob, base)) {
                out.skipped.push_back(k->label + ": not located (no game value to change)");
                continue;
            }
            v = base * (1.0 + c.pct / 100.0);
            v = std::min(std::max(v, k->min), k->max);
        } else {
            const std::string why = check_value(*k, c.value);
            if (!why.empty()) {
                out.skipped.push_back(why);
                continue;
            }
            v = c.value;
        }
        out.values[c.knob] = representable(*k, v);
    }
    return out;
}

Preset preset_from_values(const std::string& name, const Registry& reg, const std::map<std::string, double>& values,
                          const std::function<bool(const std::string&, double&)>& base_of) {
    Preset p;
    p.name = name;
    p.id = "user_" + safe_name(name);
    p.note = "Saved in the Turbo window";
    for (const Knob& k : reg.knobs) {
        auto it = values.find(k.id);
        if (it == values.end()) continue;
        double base = 0;
        const bool have = base_of && base_of(k.id, base);
        if (have && same_value(k.type, base, it->second)) continue;
        PresetChange c;
        c.knob = k.id;
        if (have && base != 0 && k.type != ValueType::Bool) {
            c.relative = true;
            c.pct = std::round((it->second / base - 1.0) * 1000.0) / 10.0;
        } else {
            c.relative = false;
            c.value = it->second;
        }
        p.changes.push_back(c);
    }
    return p;
}

// ------------------------------------------------------------------------------------------------ the service
double anchor_retry_us(int failures) {
    double t = kAnchorRetryUs;
    for (int i = 1; i < failures && t < kAnchorRetryMaxUs; ++i) t *= 2;
    return std::min(t, kAnchorRetryMaxUs);
}

CoreService::CoreService(Host h) : h_(std::move(h)) {
    std::lock_guard<std::mutex> lock(mu_);
    load_locked();
}

void CoreService::log(const std::string& s) {
    if (h_.log) h_.log(s);
}

void CoreService::load_locked() {
    anchors_.clear();
    anchor_failed_at_.clear();
    anchor_failures_.clear();
    ++anchors_epoch_;
    auto empty = std::make_shared<Registry>();
    empty->build = h_.build;
    if (!h_.off.empty()) {
        reg_ = empty;
        reg_why_ = h_.off;
        log("gameplay tuning: off (" + reg_why_ + ")");
        return;
    }
    Registry r;
    std::string err;
    if (!load_registry(h_.knobs_dir, h_.build, r, err)) {
        reg_ = empty;
        reg_why_ = err;
        log("gameplay tuning: " + err);
        return;
    }
    reg_why_.clear();
    const int ph = r.placeholders();
    log("gameplay tuning: knobs file " + r.source + " for build " + r.build + ": " + std::to_string(r.knobs.size()) + " knobs (" +
        std::to_string(int(r.knobs.size()) - ph) + " verified in the running game, " + std::to_string(ph) + " not, never written), " +
        std::to_string(r.locators.size()) + " locators; writes need turbo_output\\" + opt_in_file() + ", kill switches turbo_output\\" +
        kill_switch_file() + ", \\" + attrib_switch_file() + " and \\" + master_switch_file());
    reg_ = std::make_shared<Registry>(std::move(r));
}

std::shared_ptr<const Registry> CoreService::registry() {
    std::lock_guard<std::mutex> lock(mu_);
    return reg_;
}

std::string CoreService::registry_status() {
    std::lock_guard<std::mutex> lock(mu_);
    return reg_why_;
}

bool CoreService::reload(std::string& why) {
    std::lock_guard<std::mutex> lock(mu_);
    if (busy_) {
        why = "a gameplay tuning job is still queued or running";
        return false;
    }
    load_locked();
    why = reg_why_;
    return reg_why_.empty();
}

void CoreService::ensure_anchors() {
    std::shared_ptr<const Registry> reg;
    long long epoch = 0;
    std::vector<Locator> slow;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reg = reg_;
        epoch = anchors_epoch_;
        for (const Locator& l : reg->locators) {
            auto have = anchors_.find(l.name);
            if (have != anchors_.end()) {
                // a slow anchor that failed (the attribulator not loaded yet, a freed instance) is searched again after a back-off
                const bool retry = !have->second.ok && !have->second.pending && slow_anchor(l) && l.placeholder().empty() && h_.clock_us &&
                                   now() - anchor_failed_at_[l.name] > anchor_retry_us(anchor_failures_[l.name]);
                if (!retry) continue;
            }
            const bool background = l.placeholder().empty() && slow_anchor(l) && h_.mem &&
                                    (l.kind == "attrib" ? bool(h_.lookup.refs) : bool(h_.lookup.scan));
            if (background) {
                Anchor a;
                a.pending = true;
                a.why = l.kind == "attrib" ? "searching the game's memory for its attribulator data ..." : "resolving its pattern in FC27.exe ...";
                anchors_[l.name] = a;
                slow.push_back(l);
            } else {
                Memory* m = h_.mem;
                NullMemory none;
                anchors_[l.name] = resolve_anchor(m ? *m : none, l, h_.lookup);
                const Anchor& a = anchors_[l.name];
                if (!a.ok) {
                    anchor_failed_at_[l.name] = now();
                    ++anchor_failures_[l.name];
                } else {
                    anchor_failures_.erase(l.name);
                }
                if (l.placeholder().empty())
                    log("gameplay tuning: locator " + l.name + (a.ok ? " anchored at " + hex(a.addr) : " not resolved: " + a.why));
            }
        }
    }
    if (slow.empty()) return;
    auto job = [this, slow, epoch]() {
        // the attribulator searches of every locator in one go: one pass over the image for the type names, one over the heap for the
        // instance names (each pass reads the whole memory once, whatever the number of names)
        Lookup lk = h_.lookup;
        std::vector<std::string> types, insts;
        for (const Locator& l : slow)
            if (l.kind == "attrib") {
                types.push_back(l.type);
                if (l.l3()) {
                    types.push_back(l.db_type);
                    if (!l.group_type.empty()) types.push_back(l.group_type);
                }
                for (const Variant& v : l.variants) insts.push_back(v.db.empty() ? v.instance : v.db);
            }
        if (!types.empty() && h_.lookup.refs) {
            std::map<std::string, std::vector<uint64_t>> tmap, imap;
            try {
                tmap = h_.lookup.refs(types, true);
                imap = h_.lookup.refs(insts, false);
            } catch (...) {
            }
            lk.refs = [tmap, imap](const std::vector<std::string>& names, bool in_image) {
                std::map<std::string, std::vector<uint64_t>> out;
                const auto& src = in_image ? tmap : imap;
                for (const std::string& n : names) {
                    auto it = src.find(n);
                    if (it != src.end()) out[n] = it->second;
                }
                return out;
            };
        }
        for (const Locator& l : slow) {
            const Anchor a = resolve_anchor(*h_.mem, l, lk);
            std::string where;
            for (const Copy& c : a.copies) where += (where.empty() ? "" : ", ") + c.variant + " " + hex(c.base);
            log("gameplay tuning: locator " + l.name +
                (a.ok ? (l.kind == "attrib" ? " found: type " + l.type + ", " + where : " anchored at " + hex(a.addr) + " (inline pattern)") : " not resolved: " + a.why));
            std::lock_guard<std::mutex> lock(mu_);
            if (anchors_epoch_ != epoch) continue;
            anchors_[l.name] = a;
            if (!a.ok) {
                anchor_failed_at_[l.name] = now();
                ++anchor_failures_[l.name];
            } else {
                anchor_failures_.erase(l.name);
            }
        }
    };
    if (h_.run_background) h_.run_background(job);
    else job();
}

std::string CoreService::anchors_status() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!reg_why_.empty()) return reg_why_;
    int ok = 0, pending = 0, ready = 0;
    std::string first;
    for (const Locator& l : reg_->locators) {
        if (!l.placeholder().empty()) continue;
        ++ready;
        auto it = anchors_.find(l.name);
        if (it == anchors_.end()) continue;
        if (it->second.ok) ++ok;
        else if (it->second.pending) ++pending;
        else if (first.empty()) first = l.name + ": " + it->second.why;
    }
    const int ph = int(reg_->locators.size()) - ready;
    std::string s = std::to_string(ok) + " of " + std::to_string(ready) + " locators resolved";
    if (ph) s += ", " + std::to_string(ph) + " placeholder" + (ph == 1 ? "" : "s");
    if (pending) s += ", " + std::to_string(pending) + " resolving ...";
    if (!first.empty()) s += " (" + first + ")";
    if (reg_->locators.empty()) s = "no locator in the knobs file yet";
    return s;
}

std::vector<KnobLive> CoreService::read() {
    ensure_anchors();
    std::shared_ptr<const Registry> reg;
    AnchorMap anchors;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reg = reg_;
        anchors = anchors_;
    }
    std::vector<KnobLive> out;
    for (const Knob& k : reg->knobs) {
        KnobLive l;
        l.id = k.id;
        if (!h_.mem) {
            l.why = "no game memory";
        } else {
            KnobAt at;
            l.why = locate_knob(*h_.mem, *reg, k, anchors, h_.lookup, at);
            l.located = l.why.empty();
            l.addr = at.addr;
            l.value = at.value;
            l.copies = at.copies;
            l.writable = l.located && k.has_range();
            if (l.why.find("locate again") != std::string::npos) {
                // an attribulator instance was freed or moved: its locator is searched again
                std::lock_guard<std::mutex> lock(mu_);
                if (anchors_.count(k.locator) && anchors_[k.locator].ok) {
                    anchors_.erase(k.locator);
                    log("gameplay tuning: locator " + k.locator + " lost its data (" + l.why + "): searching again");
                }
            }
        }
        out.push_back(l);
    }
    return out;
}

bool CoreService::submit(const Context& ctx, const Plan& plan_in, std::string& why) {
    why.clear();
    note_context(ctx);
    if (!h_.run_on_game_thread) {
        why = "no game-thread dispatcher";
        return false;
    }
    if (!h_.mem) {
        why = "no game memory";
        return false;
    }
    Plan plan = plan_in;
    if (plan.load_gen < 0) plan.load_gen = ctx.load_gen;
    if (plan.load_gen != ctx.load_gen) {
        why = "the plan was made in career load " + std::to_string(plan.load_gen) + ", this is load " + std::to_string(ctx.load_gen);
        return false;
    }
    std::shared_ptr<const Registry> reg;
    AnchorMap anchors;
    int seq = 0;
    std::shared_ptr<std::atomic<bool>> cancel;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!reg_why_.empty()) {
            why = reg_why_;
            return false;
        }
        if (!session_off_.empty() && !puts_back(plan.kind)) {
            why = "gameplay tuning is off for this session: " + session_off_;
            return false;
        }
        if (busy_) {
            if (h_.clock_us && now() - busy_since_ > kJobLostUs) {
                if (cancel_) cancel_->store(true);
                log("gameplay tuning: the last job did not come back in " + std::to_string(int(kJobLostUs / 1e6)) +
                    " s (dropped by the game-thread queue?): given up (if it still runs it writes nothing)");
                busy_ = false;
            } else {
                why = "a gameplay tuning job is still queued or running";
                return false;
            }
        }
        reg = reg_;
        anchors = anchors_;
        busy_ = true;
        busy_since_ = now();
        seq = ++job_seq_;
        cancel_ = std::make_shared<std::atomic<bool>>(false);
        cancel = cancel_;
    }
    // the switch files once more right before queuing (no file I/O inside the job): tactics_off.txt, tuning_off.txt, call_attrib_off.txt
    // and the opt-in. Undo / Restore game defaults put the game's values back: they are not blocked
    std::string switch_block;
    if (!puts_back(plan.kind)) {
        if (h_.out_dir.empty()) {
            switch_block = std::string("no turbo_output folder (") + opt_in_file() + ")";
        } else {
            const Switches sw = read_switches(h_.out_dir);
            if (!sw.blocking().empty()) switch_block = sw.blocking();
            else if (!sw.opt_in) switch_block = std::string("no ") + opt_in_file();
        }
    }
    const double queued = now();
    const std::string via = h_.run_on_game_thread([this, reg, anchors, plan, switch_block, queued, seq, cancel]() {
        ApplyResult r;
        r.kind = plan.kind;
        const double t0 = now();
        std::string stage, block;
        if (cancel->load()) {
            stage = "cancelled";
            block = "the job was given up before the game thread ran it";
        } else if (h_.clock_us && t0 - queued > kJobDeadlineUs) {
            char b[96];
            std::snprintf(b, sizeof(b), "the game thread ran the job %.1f s after Apply", (t0 - queued) / 1e6);
            stage = "late";
            block = b;
        } else if (!switch_block.empty()) {
            stage = "G1";
            block = "turbo_output\\" + switch_block;
        } else {
            std::lock_guard<std::mutex> lock(mu_);
            if (cur_gen_ != plan.load_gen) {
                stage = "G2";
                block = "the career was reloaded since Apply (load " + std::to_string(cur_gen_) + ", the plan's " + std::to_string(plan.load_gen) + ")";
            } else {
                for (const Write& w : plan.writes) {
                    const Knob* k = reg->knob(w.id);
                    if (k && !k->allows(cur_phase_)) {
                        stage = "G2";
                        block = k->label + " may be written in " + when_text(k->when) + " only (now: " + (cur_phase_.empty() ? "unknown" : cur_phase_) + ")";
                        break;
                    }
                }
            }
        }
        if (!block.empty()) {
            r.stage = stage;
            r.message = block + ": nothing written";
            r.log_line = std::string("gameplay tuning ") + plan_kind_name(plan.kind) + " refused (" + stage + "): " + r.message;
        } else {
            JobOptions opt;
            opt.cancel = cancel.get();
            opt.owner_check = true;
            std::map<std::string, double> last;
            {
                std::lock_guard<std::mutex> lock(mu_);
                last = last_written_;
            }
            opt.last_written = &last;
            r = apply_plan(*h_.mem, *reg, anchors, h_.lookup, plan, opt);
        }
        r.micros = h_.clock_us ? h_.clock_us() - t0 : 0.0;
        std::lock_guard<std::mutex> lock(mu_);
        // the first snapshot of a knob in this game session = its game default (Apply and re-apply only: an undo writes Turbo's values back)
        if (!puts_back(plan.kind) && (r.written || r.undone || r.restore_failed))
            for (const auto& kv : r.snapshot) defaults_.emplace(kv.first, kv.second);
        // what Turbo wrote last (the owner check of the next job): every copy written and read back
        if (r.ok && r.written)
            for (const Write& w : r.writes) last_written_[copy_key(w.id, w.copy)] = w.new_value;
        if (r.session_off) session_off_ = r.message;
        results_.push_back(r);
        if (seq == job_seq_) busy_ = false;
    });
    if (via.empty()) {
        cancel->store(true);
        std::lock_guard<std::mutex> lock(mu_);
        if (seq == job_seq_) busy_ = false;
        why = "no game-thread dispatcher (the game_tick hook is off and Turbo's Lua side does not pump)";
        return false;
    }
    return true;
}

bool CoreService::poll(ApplyResult& out) {
    ApplyResult r;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (results_.empty()) return false;
        r = results_.front();
        results_.erase(results_.begin());
    }
    if (!r.log_line.empty()) {
        char t[64];
        std::snprintf(t, sizeof(t), " [job %.0f us, %d reads]", r.micros, r.reads);
        log(r.log_line + t);
    }
    out = r;
    return true;
}

bool CoreService::busy() {
    std::lock_guard<std::mutex> lock(mu_);
    if (busy_ && h_.clock_us && now() - busy_since_ > kJobLostUs) return false;  // given up at the next submit
    return busy_;
}

std::string CoreService::session_off() {
    std::lock_guard<std::mutex> lock(mu_);
    return session_off_;
}

std::map<std::string, double> CoreService::last_written() {
    std::lock_guard<std::mutex> lock(mu_);
    return last_written_;
}

std::map<std::string, double> CoreService::defaults() {
    std::lock_guard<std::mutex> lock(mu_);
    return defaults_;
}

void CoreService::note_context(const Context& ctx) {
    std::lock_guard<std::mutex> lock(mu_);
    cur_phase_ = ctx.phase;
    cur_session_ = ctx.session;
    cur_gen_ = ctx.load_gen;
}

}  // namespace tuning
}  // namespace turbo
