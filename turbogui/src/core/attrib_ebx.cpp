// FC 27 LE Turbo GUI - attribulator EBX patch at load (see attrib_ebx.h)
#include "attrib_ebx.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"

namespace turbo {
namespace attrib {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool fail(std::string* why, const std::string& s) {
    if (why) *why = s;
    return false;
}
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
int32_t les32(const uint8_t* p) { return int32_t(le32(p)); }
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string num(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- EBX container
bool parse_ebx(const uint8_t* d, size_t n, EbxView& out, std::string* why) {
    out = EbxView();
    if (!d || n < 0x30) return fail(why, "the EBX is too short");
    if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "EBX", 3) != 0 || std::memcmp(d + 12, "EBXD", 4) != 0) return fail(why, "not a RIFF EBX (EBXD)");
    if (uint64_t(le32(d + 4)) + 8 != n) return fail(why, "the RIFF size does not match the asset's size");
    const uint64_t dsz = le32(d + 16);
    out.payload = 0x20;
    out.data_end = size_t(20 + dsz);
    const uint64_t efix = 20 + dsz + (dsz & 1);
    if (out.data_end > n || efix + 8 > n || std::memcmp(d + efix, "EFIX", 4) != 0) return fail(why, "no EFIX chunk after the data");
    const uint64_t efix_end = efix + 8 + le32(d + efix + 4);
    if (efix_end > n) return fail(why, "the EFIX chunk is cut off");
    uint64_t p = efix + 8;
    auto u32 = [&](uint32_t& v) {
        if (p + 4 > efix_end) return false;
        v = le32(d + p);
        p += 4;
        return true;
    };
    auto skip = [&](uint64_t bytes) {
        if (p + bytes > efix_end) return false;
        p += bytes;
        return true;
    };
    auto list = [&](std::vector<uint32_t>* to) {
        uint32_t c = 0;
        if (!u32(c) || c > 1000000 || p + uint64_t(c) * 4 > efix_end) return false;
        for (uint32_t i = 0; i < c; ++i) {
            uint32_t v = 0;
            u32(v);
            if (to) to->push_back(v);
        }
        return true;
    };
    uint32_t c = 0;
    if (!skip(16) || !u32(c) || !skip(uint64_t(c) * 16)) return fail(why, "EFIX: type guids");
    if (!list(&out.type_sigs)) return fail(why, "EFIX: type sigs");
    if (!u32(out.exported)) return fail(why, "EFIX: exported");
    if (!list(&out.dc)) return fail(why, "EFIX: instances");
    if (!list(&out.ptrs)) return fail(why, "EFIX: pointer slots");
    if (!list(nullptr)) return fail(why, "EFIX: resource refs");
    if (!u32(c) || !skip(uint64_t(c) * 32)) return fail(why, "EFIX: imports");
    if (!list(nullptr) || !list(nullptr)) return fail(why, "EFIX: import / typeinfo offsets");
    for (uint32_t o : out.dc)
        if (out.payload + o + 8 > out.data_end) return fail(why, "EFIX: an instance lies outside the data");
    return true;
}

std::vector<size_t> instances_of(const uint8_t* d, size_t n, const EbxView& v, uint32_t sig) {
    std::vector<size_t> r;
    for (uint32_t o : v.dc) {
        const size_t at = v.payload + o;
        if (at + 2 > n) continue;
        const uint16_t ti = le16(d + at);
        if (ti < v.type_sigs.size() && v.type_sigs[ti] == sig) r.push_back(at);
    }
    return r;
}

static bool is_ptr_slot(const EbxView& v, size_t abs) {
    if (abs < v.payload) return false;
    const uint64_t rel = abs - v.payload;
    return std::find(v.ptrs.begin(), v.ptrs.end(), uint32_t(rel)) != v.ptrs.end();
}

std::string instance_name(const uint8_t* d, size_t n, const EbxView& v, size_t inst) {
    const size_t slot = inst + kNameSlot;
    if (slot + 4 > v.data_end || !is_ptr_slot(v, slot)) return "";
    const int32_t rel = les32(d + slot);
    if (rel == 0) return "";
    const int64_t a = int64_t(slot) + rel;
    if (a < int64_t(v.payload) || uint64_t(a) >= n) return "";
    size_t e = size_t(a);
    while (e < n && d[e]) ++e;
    if (e >= n) return "";
    return std::string(reinterpret_cast<const char*>(d + a), e - size_t(a));
}

// ---------------------------------------------------------------- kinds and paths
bool kind_of(const std::string& s, Kind& k) {
    static const std::pair<const char*, Kind> t[] = {{"f32", Kind::F32},     {"i32", Kind::I32},     {"u32", Kind::U32},     {"bool", Kind::Bool},
                                                     {"af32", Kind::ArrF32}, {"ai32", Kind::ArrI32}, {"au32", Kind::ArrU32}, {"curve", Kind::Curve}};
    for (const auto& x : t)
        if (s == x.first) {
            k = x.second;
            return true;
        }
    return false;
}
const char* kind_name(Kind k) {
    switch (k) {
        case Kind::F32: return "f32";
        case Kind::I32: return "i32";
        case Kind::U32: return "u32";
        case Kind::Bool: return "bool";
        case Kind::ArrF32: return "af32";
        case Kind::ArrI32: return "ai32";
        case Kind::ArrU32: return "au32";
        case Kind::Curve: return "curve";
    }
    return "?";
}

static bool is_ident(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}
static bool parse_index(const std::string& s, size_t open, int& idx, size_t& after) {
    if (open >= s.size() || s[open] != '[') return false;
    const size_t close = s.find(']', open);
    if (close == std::string::npos || close == open + 1 || close - open > 7) return false;
    int v = 0;
    for (size_t i = open + 1; i < close; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = v * 10 + (s[i] - '0');
    }
    idx = v;
    after = close + 1;
    return true;
}

bool parse_path(const std::string& path, Kind kind, PathSpec& out, std::string* why) {
    out = PathSpec();
    const bool arr = kind == Kind::ArrF32 || kind == Kind::ArrI32 || kind == Kind::ArrU32;
    if (kind == Kind::Curve) {
        const size_t dot = path.find(".Points[");
        if (dot == std::string::npos) return fail(why, "a curve path is Field.Points[i].X or .Y: " + path);
        out.field = path.substr(0, dot);
        size_t after = 0;
        if (!is_ident(out.field) || !parse_index(path, dot + 7, out.index, after)) return fail(why, "bad curve path: " + path);
        const std::string tail = path.substr(after);
        if (tail == ".X") out.axis = 'X';
        else if (tail == ".Y") out.axis = 'Y';
        else return fail(why, "a curve path ends in .X or .Y: " + path);
        return true;
    }
    if (arr) {
        const size_t open = path.find('[');
        size_t after = 0;
        if (open == std::string::npos) return fail(why, "an array path is Field[i]: " + path);
        out.field = path.substr(0, open);
        if (!is_ident(out.field) || !parse_index(path, open, out.index, after) || after != path.size()) return fail(why, "bad array path: " + path);
        return true;
    }
    if (!is_ident(path)) return fail(why, "a single value's path is the field name: " + path);
    out.field = path;
    return true;
}

// ---------------------------------------------------------------- patching
namespace {

struct Write {
    size_t at = 0;
    Kind elem = Kind::F32;  // F32 / I32 / U32 / Bool
    double before = 0, value = 0;
    std::string what;
};

double read_elem(const uint8_t* d, size_t at, Kind k) {
    switch (k) {
        case Kind::F32: {
            float f;
            std::memcpy(&f, d + at, 4);
            return f;
        }
        case Kind::I32: return double(les32(d + at));
        case Kind::U32: return double(le32(d + at));
        case Kind::Bool: return d[at] ? 1.0 : 0.0;
        default: return 0;
    }
}
size_t elem_size(Kind k) { return k == Kind::Bool ? 1 : 4; }

// The array a pointer slot leads to: data offset + count; false when the slot is not a pointer slot or the array does not fit
bool array_at(const uint8_t* d, size_t n, const EbxView& v, size_t slot, size_t esize, size_t& data, uint32_t& count, std::string* why) {
    if (slot + 8 > v.data_end || !is_ptr_slot(v, slot)) return fail(why, "the field is not a pointer slot of the file");
    const int32_t rel = les32(d + slot);
    if (rel == 0) return fail(why, "the array is empty (null)");
    const int64_t a = int64_t(slot) + rel;
    if (a < int64_t(v.payload) + 4 || uint64_t(a) > n) return fail(why, "the array lies outside the file");
    count = le32(d + a - 4);
    if (uint64_t(a) + uint64_t(count) * esize > v.data_end) return fail(why, "the array runs past the data");
    data = size_t(a);
    return true;
}

bool locate(const uint8_t* d, size_t n, const EbxView& v, size_t inst, const Patch& p, Write& w, std::string* why) {
    PathSpec ps;
    if (!parse_path(p.path, p.kind, ps, why)) return false;
    const size_t field = inst + p.offset;
    switch (p.kind) {
        case Kind::F32:
        case Kind::I32:
        case Kind::U32:
        case Kind::Bool: {
            const size_t es = elem_size(p.kind);
            if (field + es > v.data_end) return fail(why, p.path + ": the field lies outside the data");
            if (is_ptr_slot(v, field)) return fail(why, p.path + ": the field is a pointer slot, not a value");
            w.at = field;
            w.elem = p.kind;
            return true;
        }
        case Kind::ArrF32:
        case Kind::ArrI32:
        case Kind::ArrU32: {
            const Kind ek = p.kind == Kind::ArrF32 ? Kind::F32 : p.kind == Kind::ArrI32 ? Kind::I32 : Kind::U32;
            size_t data = 0;
            uint32_t count = 0;
            std::string e;
            if (!array_at(d, n, v, field, 4, data, count, &e)) return fail(why, p.path + ": " + e);
            if (ps.index < 0 || uint32_t(ps.index) >= count) return fail(why, p.path + ": index out of range (" + std::to_string(count) + " elements)");
            w.at = data + size_t(ps.index) * 4;
            w.elem = ek;
            return true;
        }
        case Kind::Curve: {
            if (field + 8 > v.data_end || !is_ptr_slot(v, field)) return fail(why, p.path + ": the curve field is not a pointer slot of the file");
            const int32_t rel = les32(d + field);
            const int64_t c = int64_t(field) + rel;
            if (rel == 0 || c < int64_t(v.payload)) return fail(why, p.path + ": the curve pointer is null or outside the file");
            const uint64_t crel = uint64_t(c) - v.payload;
            if (std::find(v.dc.begin(), v.dc.end(), uint32_t(crel)) == v.dc.end()) return fail(why, p.path + ": the curve pointer does not lead to an instance of the file");
            size_t data = 0;
            uint32_t count = 0;
            std::string e;
            if (!array_at(d, n, v, size_t(c) + kCurvePointsSlot, kPointStride, data, count, &e)) return fail(why, p.path + ": the curve's points: " + e);
            if (ps.index < 0 || uint32_t(ps.index) >= count) return fail(why, p.path + ": point out of range (" + std::to_string(count) + " points)");
            w.at = data + size_t(ps.index) * kPointStride + (ps.axis == 'X' ? kPointX : kPointY);
            w.elem = Kind::F32;
            return true;
        }
    }
    return fail(why, "unknown kind");
}

bool value_ok(Kind elem, double v, std::string* why) {
    if (!std::isfinite(v)) return fail(why, "the value is not a finite number");
    switch (elem) {
        case Kind::F32: return std::fabs(v) <= 3.0e38 || fail(why, "the value does not fit a float");
        case Kind::I32: return (v == std::floor(v) && v >= -2147483648.0 && v <= 2147483647.0) || fail(why, "the value is not a 32-bit integer");
        case Kind::U32: return (v == std::floor(v) && v >= 0 && v <= 4294967295.0) || fail(why, "the value is not an unsigned 32-bit integer");
        case Kind::Bool: return (v == 0 || v == 1) || fail(why, "a switch is 0 or 1");
        default: return fail(why, "unknown element kind");
    }
}

void store(uint8_t* d, size_t at, Kind elem, double v) {
    switch (elem) {
        case Kind::F32: {
            const float f = float(v);
            std::memcpy(d + at, &f, 4);
            break;
        }
        case Kind::I32: {
            const int32_t i = int32_t(v);
            std::memcpy(d + at, &i, 4);
            break;
        }
        case Kind::U32: {
            const uint32_t u = uint32_t(v);
            std::memcpy(d + at, &u, 4);
            break;
        }
        case Kind::Bool: d[at] = v != 0 ? 1 : 0; break;
        default: break;
    }
}

bool same(Kind elem, double a, double b) {
    if (elem == Kind::F32) return std::fabs(a - b) <= 1e-4 * std::max(1.0, std::fabs(b));
    return a == b;
}

}  // namespace

bool patch_ebx(std::vector<uint8_t>& raw, const std::string& asset, const std::vector<Patch>& patches, std::vector<std::string>* changes, std::string* why) {
    if (changes) changes->clear();
    EbxView v;
    std::string e;
    if (!parse_ebx(raw.data(), raw.size(), v, &e)) return fail(why, e);
    std::vector<Write> writes;
    std::set<size_t> seen;
    for (const Patch& p : patches) {
        if (lower(p.asset) != lower(asset)) continue;
        const auto insts = instances_of(raw.data(), raw.size(), v, p.type_sig);
        if (insts.size() != 1)
            return fail(why, p.path + ": " + std::to_string(insts.size()) + " instances of type sig " + std::to_string(p.type_sig) + " (one expected): another layout");
        const std::string nm = instance_name(raw.data(), raw.size(), v, insts[0]);
        if (lower(nm) != lower(asset)) return fail(why, p.path + ": the instance is named \"" + nm + "\", not the asset");
        Write w;
        if (!locate(raw.data(), raw.size(), v, insts[0], p, w, &e)) return fail(why, e);
        w.before = read_elem(raw.data(), w.at, w.elem);
        if (p.has_expect && !same(w.elem, w.before, p.expect))
            return fail(why, p.path + ": the install holds " + num(w.before) + ", not " + num(p.expect) + ": another layout or a modded file");
        if (!value_ok(w.elem, p.value, &e)) return fail(why, p.path + ": " + e);
        if (!seen.insert(w.at).second) return fail(why, p.path + ": the same value is named twice");
        w.what = p.path + " " + num(w.before) + " -> " + num(p.value);
        w.value = p.value;
        writes.push_back(w);
    }
    if (writes.empty()) return fail(why, "no patch names this asset");
    for (const Write& w : writes) {
        store(raw.data(), w.at, w.elem, w.value);
        if (changes) changes->push_back(w.what);
    }
    return true;
}

// ---------------------------------------------------------------- preset
std::vector<std::string> Preset::assets() const {
    std::vector<std::string> r;
    for (const auto& p : patches)
        if (std::find(r.begin(), r.end(), p.asset) == r.end()) r.push_back(p.asset);
    return r;
}

bool parse_preset(const std::string& text, Preset& out, std::string* why) {
    out = Preset();
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(why, "attrib_preset.json is not JSON");
    try {
        if (j.value("schema", std::string()) != "turbo-attrib-preset/1") return fail(why, "attrib_preset.json: schema is not turbo-attrib-preset/1");
        out.build = j.value("build", std::string());
        if (out.build.empty()) return fail(why, "attrib_preset.json: no build");
        const std::string mode = j.value("mode", std::string("patch"));
        if (mode == "patch") out.mode = Mode::Patch;
        else if (mode == "observe") out.mode = Mode::Observe;
        else return fail(why, "attrib_preset.json: mode is patch or observe");
        if (!j.contains("patches") || !j["patches"].is_array() || j["patches"].empty()) return fail(why, "attrib_preset.json: no patches");
        int i = 0;
        for (const auto& x : j["patches"]) {
            const std::string at = "attrib_preset.json patch " + std::to_string(i++) + ": ";
            if (!x.is_object()) return fail(why, at + "not an object");
            Patch p;
            p.knob = x.value("knob", std::string());
            p.asset = x.value("asset", std::string());
            if (p.asset.rfind("fifa/attribulator/", 0) != 0) return fail(why, at + "asset must be a fifa/attribulator/ EBX");
            if (!x.contains("type_sig") || !x["type_sig"].is_number_unsigned()) return fail(why, at + "type_sig missing");
            p.type_sig = x["type_sig"].get<uint32_t>();
            p.path = x.value("path", std::string());
            if (!kind_of(x.value("kind", std::string()), p.kind)) return fail(why, at + "kind is f32, i32, u32, bool, af32, ai32, au32 or curve");
            PathSpec ps;
            std::string e;
            if (!parse_path(p.path, p.kind, ps, &e)) return fail(why, at + e);
            if (!x.contains("offset")) return fail(why, at + "offset missing");
            if (x["offset"].is_string()) {
                const std::string s = x["offset"].get<std::string>();
                char* end = nullptr;
                const unsigned long v = std::strtoul(s.c_str(), &end, 0);
                if (s.empty() || !end || *end) return fail(why, at + "offset is not a number: " + s);
                p.offset = uint32_t(v);
            } else if (x["offset"].is_number_unsigned()) {
                p.offset = x["offset"].get<uint32_t>();
            } else {
                return fail(why, at + "offset is not a number");
            }
            if (p.offset < 0x20 || p.offset > 0x10000) return fail(why, at + "offset out of range (fields start at 0x20)");
            if (!x.contains("value") || !(x["value"].is_number() || x["value"].is_boolean())) return fail(why, at + "value missing");
            p.value = x["value"].is_boolean() ? (x["value"].get<bool>() ? 1.0 : 0.0) : x["value"].get<double>();
            if (x.contains("expect")) {
                if (!(x["expect"].is_number() || x["expect"].is_boolean())) return fail(why, at + "expect is not a number");
                p.has_expect = true;
                p.expect = x["expect"].is_boolean() ? (x["expect"].get<bool>() ? 1.0 : 0.0) : x["expect"].get<double>();
            }
            if (!std::isfinite(p.value)) return fail(why, at + "value is not finite");
            out.patches.push_back(p);
        }
        if (j.contains("locations")) {
            if (!j["locations"].is_array()) return fail(why, "attrib_preset.json: locations is not a list");
            for (const auto& x : j["locations"]) {
                KnownLoc l;
                l.asset = x.value("asset", std::string());
                l.toc = x.value("toc", std::string());
                l.cas = x.value("cas", std::string());
                if (!x.contains("off") || !x["off"].is_number_unsigned() || !x.contains("size") || !x["size"].is_number_unsigned())
                    return fail(why, "attrib_preset.json: a location needs off and size");
                l.off = x["off"].get<uint64_t>();
                l.size = x["size"].get<uint32_t>();
                if (l.asset.empty() || l.toc.empty() || l.cas.size() != 16 || l.size == 0) return fail(why, "attrib_preset.json: a location needs asset, toc, cas (16 hex digits) and size");
                out.locations.push_back(l);
            }
        }
    } catch (const std::exception& ex) {
        return fail(why, std::string("attrib_preset.json: ") + ex.what());
    }
    return true;
}

// ---------------------------------------------------------------- finding the assets
std::vector<std::string> attrib_tocs() { return {"Patch/Win32/fc/fcgame/fcgame.toc", "Data/Win32/fc/fcgame/fcgame.toc"}; }

bool find_assets(kitswap::GameFiles& files, const kitswap::OodleFn& oodle, const std::vector<std::string>& names, std::vector<std::vector<AssetLoc>>& found,
                 std::string* err) {
    found.assign(names.size(), {});
    std::map<std::string, size_t> want;
    for (size_t i = 0; i < names.size(); ++i) want[lower(names[i])] = i;
    int tocs = 0;
    for (const std::string& rel : attrib_tocs()) {
        std::vector<uint8_t> toc;
        if (!files.read_file(rel, toc)) continue;
        std::vector<std::vector<kitswap::Loc>> bundles;
        if (!kitswap::parse_toc_bundles(toc, bundles, nullptr)) continue;
        ++tocs;
        toc.clear();
        toc.shrink_to_fit();
        std::vector<uint8_t> raw, blob;
        for (const auto& locs : bundles) {
            if (locs.empty() || locs[0].size < 40) continue;
            if (!files.read_loc(locs[0], raw)) continue;
            kitswap::Manifest m;
            if (raw.size() > 8 && le32(&raw[4]) == 0x9D798ED6u) {
                if (!kitswap::parse_manifest(raw, m, nullptr)) continue;
            } else if (!kitswap::read_blocks(raw, oodle, blob, nullptr) || !kitswap::parse_manifest(blob, m, nullptr)) {
                continue;
            }
            for (size_t i = 0; i < m.ebx.size(); ++i) {
                if (m.ebx[i].rfind("fifa/attribulator/", 0) != 0) continue;
                auto w = want.find(lower(m.ebx[i]));
                if (w == want.end() || 1 + i >= locs.size()) continue;
                auto& v = found[w->second];
                bool dup = false;
                for (const auto& x : v) dup = dup || (x.loc.cas == locs[1 + i].cas && x.loc.off == locs[1 + i].off && x.loc.size == locs[1 + i].size);
                if (!dup) v.push_back({rel, locs[1 + i]});
            }
        }
    }
    if (tocs == 0) return fail(err, "neither Patch nor Data fcgame.toc could be read");
    return true;
}

std::string tocs_signature(const kitswap::GameFiles& files) {
    std::ostringstream o;
    for (const std::string& rel : attrib_tocs()) {
        std::error_code ec;
        const fs::path p = files.root() / fs::path(rel);
        const auto sz = fs::file_size(p, ec);
        if (ec) {
            o << rel << ":-;";
            continue;
        }
        const auto t = fs::last_write_time(p, ec);
        o << rel << ":" << sz << ":" << (ec ? 0 : int64_t(t.time_since_epoch().count())) << ";";
    }
    return o.str();
}

bool save_locs(const fs::path& file, const std::string& sig, const std::vector<std::string>& names, const std::vector<std::vector<AssetLoc>>& found) {
    json j;
    j["schema"] = "turbo-attrib-locs/1";
    j["signature"] = sig;
    json a = json::object();
    for (size_t i = 0; i < names.size() && i < found.size(); ++i) {
        json l = json::array();
        for (const auto& x : found[i]) l.push_back({{"toc", x.toc}, {"cas", x.loc.cas}, {"off", x.loc.off}, {"size", x.loc.size}});
        a[names[i]] = l;
    }
    j["assets"] = a;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump(1);
        if (!f) return false;
    }
    fs::rename(tmp, file, ec);
    return !ec;
}

bool load_locs(const fs::path& file, const std::string& sig, const std::vector<std::string>& names, std::vector<std::vector<AssetLoc>>& found) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", std::string()) != "turbo-attrib-locs/1" || j.value("signature", std::string()) != sig) return false;
    if (!j.contains("assets") || !j["assets"].is_object()) return false;
    std::vector<std::vector<AssetLoc>> out(names.size());
    try {
        for (size_t i = 0; i < names.size(); ++i) {
            if (!j["assets"].contains(names[i])) return false;  // a name the cache never looked for: search again
            for (const auto& x : j["assets"][names[i]]) {
                AssetLoc a;
                a.toc = x.at("toc").get<std::string>();
                a.loc.cas = x.at("cas").get<std::string>();
                a.loc.off = x.at("off").get<uint64_t>();
                a.loc.size = x.at("size").get<uint32_t>();
                if (a.loc.cas.size() != 16 || a.loc.size == 0) return false;
                out[i].push_back(a);
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    found = std::move(out);
    return true;
}

bool verify_asset(const std::vector<uint8_t>& stream, const kitswap::OodleFn& oodle, const std::string& asset, std::string* why) {
    std::vector<kitswap::Block> bl;
    if (!kitswap::block_headers(stream, bl) || bl.size() != 1) return fail(why, "not one block");
    if (bl[0].ctype != 0x19 && bl[0].ctype != 0x15 && bl[0].ctype != 0x11) return fail(why, "not an Oodle block");
    std::vector<uint8_t> raw;
    std::string e;
    if (!kitswap::read_blocks(stream, oodle, raw, &e)) return fail(why, "does not decompress (" + e + ")");
    EbxView v;
    if (!parse_ebx(raw.data(), raw.size(), v, &e)) return fail(why, e);
    for (uint32_t o : v.dc)
        if (lower(instance_name(raw.data(), raw.size(), v, v.payload + o)) == lower(asset)) return true;
    return fail(why, "no instance is named " + asset);
}

bool known_locations(const Preset& p, const std::vector<std::string>& names, std::vector<std::vector<AssetLoc>>& found) {
    std::vector<std::vector<AssetLoc>> out(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        for (const auto& l : p.locations)
            if (lower(l.asset) == lower(names[i])) {
                AssetLoc a;
                a.toc = l.toc;
                a.loc.cas = l.cas;
                a.loc.off = l.off;
                a.loc.size = l.size;
                out[i].push_back(a);
            }
        if (out[i].empty()) return false;
    }
    found = std::move(out);
    return true;
}

// ---------------------------------------------------------------- table
bool add_asset(kitswap::TableBuilder& tb, const std::vector<uint8_t>& stream, const kitswap::OodleFn& oodle, const std::string& asset, const std::string& toc,
               const std::vector<Patch>& patches, Mode mode, ItemInfo& out, std::string* why) {
    out = ItemInfo();
    out.asset = asset;
    out.toc = toc;
    std::vector<kitswap::Block> bl;
    if (!kitswap::block_headers(stream, bl) || bl.empty()) return fail(why, "the asset's block stream does not parse");
    if (bl.size() != 1) return fail(why, "the asset is " + std::to_string(bl.size()) + " blocks (one Oodle block expected)");
    const kitswap::Block& b = bl[0];
    if (b.ctype != 0x19 && b.ctype != 0x15 && b.ctype != 0x11) return fail(why, "the asset's block is not Oodle-compressed (type " + std::to_string(b.ctype) + ")");
    if (b.csize < 64) return fail(why, "the asset's block is too small to recognise safely");
    auto raw = std::make_shared<std::vector<uint8_t>>();
    std::string e;
    if (!kitswap::read_blocks(stream, oodle, *raw, &e) || raw->size() != b.dsize) return fail(why, "the asset does not decompress (" + e + ")");
    const uint8_t* comp = &stream[size_t(b.payload)];
    uint64_t head = 0;
    std::memcpy(&head, comp, 8);
    const uint64_t fnv = kitswap::fnv1a64(comp, b.csize);
    out.csize = b.csize;
    out.dsize = b.dsize;
    bool do_patch = false;
    if (mode == Mode::Patch) {
        std::vector<std::string> ch;
        if (patch_ebx(*raw, asset, patches, &ch, &e)) {
            do_patch = true;
            std::string t;
            for (const auto& c : ch) t += (t.empty() ? "" : ", ") + c;
            out.what = "patched: " + t;
        } else {
            out.what = "watch only: " + e;
        }
    } else {
        out.what = "watch only: observe mode";
    }
    const uint32_t item = tb.add_item(do_patch ? raw : nullptr, do_patch);
    if (!tb.add_block(item, b.csize, b.dsize, head, fnv, do_patch ? raw->data() : nullptr, nullptr)) return fail(why, "the table refused the block");
    out.patch = do_patch;
    out.table_item = item;
    return true;
}

// ---------------------------------------------------------------- runtime
std::string clock_text(uint64_t ms) {
    if (!ms) return "-";
    const time_t t = time_t(ms / 1000);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[48];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, int(ms % 1000));
    return b;
}

Runtime::Runtime() = default;

uint64_t Runtime::now_ms() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void Runtime::set_expected_sizes(const std::vector<std::pair<uint32_t, uint32_t>>& sizes) {
    if (n_expect_.load()) return;  // once: the detour may be reading the list
    uint32_t n = 0;
    for (const auto& s : sizes) {
        if (n >= 16) break;
        bool dup = false;
        for (uint32_t i = 0; i < n; ++i) dup = dup || (expect_[i].csize == s.first && expect_[i].dsize == s.second);
        if (dup) continue;
        expect_[n].csize = s.first;
        expect_[n].dsize = s.second;
        ++n;
    }
    n_expect_.store(n, std::memory_order_release);
}

void Runtime::publish(std::shared_ptr<kitswap::Table> table, std::vector<ItemInfo> items) {
    std::lock_guard<std::mutex> lk(m_);
    if (keep_) return;  // published once per process: a detour may hold the pointer
    items_ = std::move(items);
    reported_.assign(items_.size(), 0);
    keep_ = std::move(table);
    published_ms_ = now_ms();
    table_.store(keep_.get(), std::memory_order_release);
}

int Runtime::on_decompressed(const uint8_t* src, uint64_t srclen, uint8_t* dst, uint64_t dstlen, int64_t ret) {
    const kitswap::Table* t = table_.load(std::memory_order_acquire);
    if (!t || killed_.load(std::memory_order_relaxed) || !src || !t->may_match(srclen, dstlen)) return 0;
    const kitswap::Entry* e = t->match(src, srclen, dstlen);
    if (!e) return 0;
    hits_.fetch_add(1, std::memory_order_relaxed);
    t->count_hit(*e);
    if (ret != int64_t(dstlen)) {
        unserved_.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (!t->swap_item(e->item) || !dst || !e->data) return 1;
    std::memcpy(dst, e->data, size_t(e->dsize));
    patched_.fetch_add(1, std::memory_order_relaxed);
    return 2;
}

std::vector<std::string> Runtime::take_seen_lines() {
    std::vector<std::string> r;
    const kitswap::Table* t = table_.load(std::memory_order_acquire);
    if (!t) return r;
    std::lock_guard<std::mutex> lk(m_);
    for (size_t i = 0; i < items_.size(); ++i) {
        const ItemInfo& it = items_[i];
        if (reported_[i] || it.table_item == 0xFFFFFFFFu) continue;
        const uint64_t first = t->first_ms(it.table_item);
        if (!first) continue;
        reported_[i] = 1;
        r.push_back("attrib block seen " + clock_text(first) + " " + it.asset + " (" + it.toc + "; " + it.what + ")");
    }
    return r;
}

std::string Runtime::log_text(const std::string& head) const {
    std::ostringstream o;
    o << "Turbo attrib EBX patch log (turbo_output\\attrib_ebx_log.txt)\n" << head << "\n";
    const kitswap::Table* t = table_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lk(m_);
    o << "table " << (t ? "published " + clock_text(published_ms_) : std::string("NOT published")) << (killed_.load() ? " [kill switch attrib_ebx_off.txt]" : "")
      << " | decompress calls " << calls_.load() << " (before the table: " << calls_before_.load() << ") | attrib blocks recognised " << hits_.load()
      << " | overwritten " << patched_.load() << " | recognised, other size returned " << unserved_.load() << "\n";
    o << "calls with an expected asset's sizes BEFORE the table was published: " << early_.load()
      << (early_.load() ? " (first " + clock_text(early_first_ms_.load()) + ": the game decompressed it before Turbo's table was ready)" : std::string()) << "\n";
    for (const auto& it : items_) {
        const uint64_t h = t && it.table_item != 0xFFFFFFFFu ? t->hits(it.table_item) : 0;
        o << (it.patch ? "  patch  " : "  watch  ") << it.asset << " [" << it.toc << ", " << it.csize << "/" << it.dsize << " B]  seen " << h;
        if (h) o << "  first " << clock_text(t->first_ms(it.table_item)) << "  last " << clock_text(t->last_ms(it.table_item));
        o << "  (" << it.what << ")\n";
    }
    return o.str();
}

namespace {
std::atomic<Runtime*> g_active{nullptr};
}
Runtime* active_runtime() { return g_active.load(std::memory_order_acquire); }
void set_active_runtime(Runtime* rt) { g_active.store(rt, std::memory_order_release); }

}  // namespace attrib
}  // namespace turbo
