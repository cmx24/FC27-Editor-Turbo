// FC 27 LE Turbo GUI - faces3d (core/faces3d.h). The generator is a byte-for-byte port of
// turbo_dev/research/c2_lua_loader/build_gated_v2.py build() / entries_lua() / parse_template().
#include "core/faces3d.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include "core/faces3d_data.h"
#include "core/lua_loader.h"
#include "nlohmann/json.hpp"

namespace turbo::faces3d {

namespace fs = std::filesystem;
using lualoader::md5_hex;

const char* const kPlayersRel = "faces3d\\players.json";
const char* const kOverrideRel = "lua_override\\player_kit.lua";
const char* const kOptInRel = "faces3d_on.txt";

static std::string md5s(const std::string& s) { return md5_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

static fs::path rel_path(const std::string& base_utf8, const char* rel) {
    fs::path p = fs::u8path(base_utf8);
    std::string r = rel;
    size_t a = 0;
    while (a <= r.size()) {
        const size_t b = r.find('\\', a);
        p /= r.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return p;
}

// ---------------------------------------------------------------- players.json
static bool positive_key(const std::string& k, int64_t& id) {
    if (k.empty() || k.size() > 12 || k[0] == '0') return false;
    for (char c : k)
        if (c < '0' || c > '9') return false;
    id = std::stoll(k);
    return id > 0;
}

bool parse_players_json(const std::string& bytes, Table& out, std::string* err) {
    auto fail = [&](const std::string& e) {
        if (err) *err = "players.json: " + e;
        return false;
    };
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(bytes);
    } catch (const std::exception& e) {
        return fail(std::string("not valid JSON: ") + e.what());
    }
    if (!j.is_object()) return fail("must be an object {\"<playerid>\": {...}}");
    Table t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        int64_t id = 0;
        if (!positive_key(it.key(), id)) return fail("key \"" + it.key() + "\" is not a player id");
        const auto& e = it.value();
        if (!e.is_object()) return fail(it.key() + ": entry must be an object");
        Entry en;
        bool colours = true, have_hc = false, have_bc = false;
        for (auto f = e.begin(); f != e.end(); ++f) {
            const std::string& k = f.key();
            const auto& v = f.value();
            if (k == "hair" || k == "beard") {
                if (v.is_null()) continue;
                if (!v.is_number_integer() || v.get<int64_t>() < 1)
                    return fail(it.key() + ": " + k + " must be a positive integer model id, got " + v.dump());
                (k == "hair" ? en.hair : en.beard) = v.get<int64_t>();
            } else if (k == "colours" || k == "hair_colour" || k == "beard_colour") {
                if (v.is_null()) continue;
                if (!v.is_boolean()) return fail(it.key() + ": " + k + " must be true/false (colour codes are not supported), got " + v.dump());
                if (k == "colours") colours = v.get<bool>();
                else if (k == "hair_colour") en.hair_colour = v.get<bool>(), have_hc = true;
                else en.beard_colour = v.get<bool>(), have_bc = true;
            } else if (k == "name") {
                if (!v.is_string()) return fail(it.key() + ": name must be text");
                en.name = v.get<std::string>();
            } else {
                return fail(it.key() + ": unknown field \"" + k + "\"");
            }
        }
        if (!have_hc) en.hair_colour = colours;
        if (!have_bc) en.beard_colour = colours;
        t[id] = en;
    }
    out = std::move(t);
    return true;
}

std::string players_json(const Table& t) {
    std::string s = "{\n";
    size_t i = 0;
    for (const auto& [id, e] : t) {
        s += "  \"" + std::to_string(id) + "\": {";
        std::string f;
        if (e.hair > 0) f += "\"hair\": " + std::to_string(e.hair) + ", ";
        if (e.beard > 0) f += "\"beard\": " + std::to_string(e.beard) + ", ";
        f += std::string("\"hair_colour\": ") + (e.hair_colour ? "true" : "false") + ", ";
        f += std::string("\"beard_colour\": ") + (e.beard_colour ? "true" : "false");
        if (!e.name.empty()) f += ", \"name\": " + nlohmann::json(e.name).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        s += f + "}" + (++i < t.size() ? "," : "") + "\n";
    }
    return s + "}\n";
}

// ---------------------------------------------------------------- verified ids
const std::vector<int>& ok_hair_ids() {
    static const std::vector<int> v(std::begin(data::kHairOk), std::end(data::kHairOk));
    return v;
}
const std::vector<int>& ok_beard_ids() {
    static const std::vector<int> v(std::begin(data::kBeardOk), std::end(data::kBeardOk));
    return v;
}
bool hair_id_ok(int64_t id) { return std::binary_search(ok_hair_ids().begin(), ok_hair_ids().end(), static_cast<int>(id)) && id > 0; }
bool beard_id_ok(int64_t id) { return std::binary_search(ok_beard_ids().begin(), ok_beard_ids().end(), static_cast<int>(id)) && id > 0; }

std::vector<std::string> validate(const Table& t, const std::function<int64_t(int64_t)>& headclass) {
    std::vector<std::string> v;
    if (t.size() > kMaxEntries) v.push_back("too many players (" + std::to_string(t.size()) + ", at most " + std::to_string(kMaxEntries) + ")");
    for (const auto& [id, e] : t) {
        const std::string who = "player " + std::to_string(id) + (e.name.empty() ? "" : " (" + e.name + ")");
        if (e.hair <= 0 && e.beard <= 0) v.push_back(who + ": choose a hair or a beard (or un-enrol him)");
        if (e.hair > 0 && !hair_id_ok(e.hair)) v.push_back(who + ": hair " + std::to_string(e.hair) + " is not a verified generic hair id");
        if (e.beard > 0 && !beard_id_ok(e.beard)) v.push_back(who + ": beard " + std::to_string(e.beard) + " is not a verified generic beard id");
        if (headclass) {
            const int64_t hc = headclass(id);
            if (hc < 0) v.push_back(who + ": not in the database");
            else if (hc != 0) v.push_back(who + ": not a scanned head (head class " + std::to_string(hc) + "); only scanned heads are supported");
        }
    }
    return v;
}

// ---------------------------------------------------------------- template
const char* template_text() { return data::kTemplate; }
const char* template_md5() { return data::kTemplateMd5; }

namespace {

struct Ins {
    long long n;
    std::string h;
    long long col;
    std::string t;
};
struct Add {
    long long n;
    std::string h, t;
};
struct Spec {
    std::string md5;
    long long gate_n = 0;
    std::string gate_h;
    bool have_gate = false;
    std::vector<Ins> ins;
    std::vector<Add> add;
    std::string gate;  // the gate block, LF, ends with '\n'
};

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

std::vector<std::string> split(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(sep, a);
        if (b == std::string::npos) {
            out.push_back(s.substr(a));
            return out;
        }
        out.push_back(s.substr(a, b - a));
        a = b + sep.size();
    }
}

// Python str.split("\t", maxsplit)
std::vector<std::string> split_max(const std::string& s, char sep, size_t maxsplit) {
    std::vector<std::string> out;
    size_t a = 0;
    while (out.size() < maxsplit) {
        const size_t b = s.find(sep, a);
        if (b == std::string::npos) break;
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    out.push_back(s.substr(a));
    return out;
}

std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool to_int(const std::string& s0, long long& v) {
    const std::string s = strip(s0);
    if (s.empty() || s.size() > 12) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    v = std::stoll(s);
    return true;
}

bool text_field(const std::string& v, const std::string& raw, std::string& out, std::string& err) {
    if (v.empty() || v.back() != '$') {
        err = "template: TEXT must end with '$' (" + raw + ")";
        return false;
    }
    out = v.substr(0, v.size() - 1);
    return true;
}

bool parse_template(const std::string& text0, Spec& sp, std::string& err) {
    const std::string text = replace_all(text0, "\r\n", "\n");
    const std::string b0 = "\nSPLICE SPEC\n", e0 = "\nEND SPEC\n";
    const size_t sb = text.find(b0);
    const size_t se = sb == std::string::npos ? std::string::npos : text.find(e0, sb + b0.size());
    if (sb == std::string::npos || se == std::string::npos) {
        err = "template: no SPLICE SPEC section";
        return false;
    }
    const std::string body = text.substr(sb + b0.size(), se - (sb + b0.size()));
    for (const std::string& raw : split(body, "\n")) {
        if (strip(raw).empty()) continue;
        const auto kr = split_max(raw, '\t', 1);
        const std::string key = kr[0], rest = kr.size() > 1 ? kr[1] : std::string();
        if (key == "@stock_md5") {
            sp.md5 = strip(rest);
        } else if (key == "@gate_after") {
            const auto p = split(rest, "\t");
            if (p.size() != 2 || !to_int(p[0], sp.gate_n)) {
                err = "template: bad @gate_after (" + raw + ")";
                return false;
            }
            sp.gate_h = p[1];
            sp.have_gate = true;
        } else if (key == "@ins") {
            const auto p = split_max(rest, '\t', 3);
            Ins in;
            if (p.size() != 4 || !to_int(p[0], in.n) || !to_int(p[2], in.col)) {
                err = "template: bad @ins (" + raw + ")";
                return false;
            }
            in.h = p[1];
            if (!text_field(p[3], raw, in.t, err)) return false;
            sp.ins.push_back(in);
        } else if (key == "@add") {
            const auto p = split_max(rest, '\t', 2);
            Add ad;
            if (p.size() != 3 || !to_int(p[0], ad.n)) {
                err = "template: bad @add (" + raw + ")";
                return false;
            }
            ad.h = p[1];
            if (!text_field(p[2], raw, ad.t, err)) return false;
            sp.add.push_back(ad);
        } else {
            err = "template: unknown directive '" + key + "'";
            return false;
        }
    }
    if (sp.md5.empty() || !sp.have_gate) {
        err = "template: @stock_md5 and @gate_after are required";
        return false;
    }
    // re.search(r"\n--@@GATE-BEGIN\n(.*?\n)--@@GATE-END", text, re.S)
    const std::string gb = "\n--@@GATE-BEGIN\n", ge = "\n--@@GATE-END";
    const size_t g0 = text.find(gb);
    const size_t g1 = g0 == std::string::npos ? std::string::npos : text.find(ge, g0 + gb.size());
    if (g1 == std::string::npos) {
        err = "template: gate block with exactly one --@@ENTRIES@@ marker required";
        return false;
    }
    sp.gate = text.substr(g0 + gb.size(), g1 + 1 - (g0 + gb.size()));
    size_t count = 0;
    for (size_t p = sp.gate.find("--@@ENTRIES@@"); p != std::string::npos; p = sp.gate.find("--@@ENTRIES@@", p + 1)) ++count;
    if (count != 1) {
        err = "template: gate block with exactly one --@@ENTRIES@@ marker required";
        return false;
    }
    return true;
}

std::string sanitize_name(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (std::isalnum(c) && c < 0x80) o += static_cast<char>(c);
        else if (c == ' ' || c == '.' || c == '_' || c == '\'' || c == '-') o += static_cast<char>(c);
    return o.substr(0, 40);
}

bool entries_lua(const Table& t, std::string& out, std::string& err) {
    std::vector<std::string> lines;
    for (const auto& [id, e] : t) {
        if (id <= 0) {
            err = "player id " + std::to_string(id) + " is not valid";
            return false;
        }
        if (e.hair < 0 || e.beard < 0) {
            err = "player " + std::to_string(id) + ": model ids must be positive";
            return false;
        }
        if (e.hair == 0 && e.beard == 0) continue;  // an entry with neither does nothing; leave it out
        std::string parts;
        if (e.beard) parts += "beard = " + std::to_string(e.beard);
        if (e.hair) parts += std::string(parts.empty() ? "" : ", ") + "hair = " + std::to_string(e.hair);
        parts += std::string(parts.empty() ? "" : ", ") + "hc = " + (e.hair_colour ? "true" : "false");
        parts += std::string(", bc = ") + (e.beard_colour ? "true" : "false");
        const std::string name = sanitize_name(e.name);
        lines.push_back("\t\t[" + std::to_string(id) + "] = { " + parts + " }," + (name.empty() ? "" : "  -- " + name));
    }
    out.clear();
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return true;
}

}  // namespace

std::string template_stock_md5(const std::string& tmpl) {
    Spec sp;
    std::string err;
    return parse_template(tmpl, sp, err) ? sp.md5 : std::string();
}

Built generate_body(const std::string& tmpl, const std::string& stock, const Table& t) {
    Built r;
    Spec sp;
    if (!parse_template(tmpl, sp, r.error)) return r;
    const std::string got = md5s(stock);
    if (got != sp.md5) {
        r.error = "stock md5 " + got + " != template " + sp.md5;
        return r;
    }
    const std::string nl = stock.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::vector<std::string> L = split(stock, nl);
    auto anchor = [&](long long n, const std::string& h) {
        if (n < 1 || n > static_cast<long long>(L.size()) || md5s(L[n - 1]) != h) {
            r.error = "anchor md5 mismatch at stock line " + std::to_string(n);
            return false;
        }
        return true;
    };
    if (!anchor(sp.gate_n, sp.gate_h)) return r;
    for (const auto& in : sp.ins) {
        if (!anchor(in.n, in.h)) return r;
        if (in.col < 0 || in.col > static_cast<long long>(L[in.n - 1].size())) {
            r.error = "column " + std::to_string(in.col) + " out of range at line " + std::to_string(in.n);
            return r;
        }
    }
    for (const auto& ad : sp.add)
        if (!anchor(ad.n, ad.h)) return r;
    std::string entries;
    if (!entries_lua(t, entries, r.error)) return r;

    std::vector<std::string> nw = L;
    std::set<long long> ins_lines;
    for (const auto& in : sp.ins) ins_lines.insert(in.n);
    for (long long n : ins_lines) {
        std::string s = L[n - 1];
        std::vector<Ins> here;
        for (const auto& in : sp.ins)
            if (in.n == n) here.push_back(in);
        std::stable_sort(here.begin(), here.end(), [](const Ins& a, const Ins& b) { return a.col > b.col; });
        for (const auto& in : here) s = s.substr(0, in.col) + in.t + s.substr(in.col);
        nw[n - 1] = s;
    }
    for (const auto& ad : sp.add) nw[ad.n - 1] += nl + ad.t;
    std::string gate = replace_all(sp.gate, "--@@ENTRIES@@\n", !t.empty() ? entries + "\n" : std::string());
    while (!gate.empty() && gate.back() == '\n') gate.pop_back();
    gate = replace_all(gate, "\n", nl);

    std::string out;
    const size_t n0 = static_cast<size_t>(sp.gate_n);
    for (size_t i = 0; i < n0; ++i) out += (i ? nl : "") + nw[i];
    out += nl + gate + nl;
    for (size_t i = n0; i < nw.size(); ++i) out += (i > n0 ? nl : "") + nw[i];
    r.bytes = std::move(out);
    r.md5 = md5s(r.bytes);
    r.ok = true;
    return r;
}

std::string table_hash(const std::string& players_json_bytes) { return md5s(players_json_bytes).substr(0, 12); }

Built generate_override(const std::string& tmpl, const std::string& stock, const Table& t, const std::string& build_key,
                        const std::string& players_json_bytes) {
    Built r;
    if (build_key.empty() || build_key.find_first_of(" \t\r\n") != std::string::npos) {
        r.error = "the game build is unknown (start the game with Turbo, then try again)";
        return r;
    }
    Built b = generate_body(tmpl, stock, t);
    if (!b.ok) return b;
    r.bytes = lualoader::make_override_header(build_key, md5s(stock), table_hash(players_json_bytes)) + "\r\n" + b.bytes;
    // the hook's own parser must accept what we wrote
    const lualoader::OverrideHeader h = lualoader::parse_override_header(r.bytes);
    if (!h.ok || h.build != build_key || h.stock != md5s(stock)) {
        r.error = "internal: the generated header does not parse back (" + h.error + ")";
        return r;
    }
    r.md5 = md5s(r.bytes);
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- files
static bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

bool load_players(const std::string& turbo_output_utf8, Table& out, std::string* err) {
    const fs::path p = rel_path(turbo_output_utf8, kPlayersRel);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        out.clear();
        return true;
    }
    std::string b;
    if (!read_file(p, b)) {
        if (err) *err = "cannot read " + p.u8string();
        return false;
    }
    return parse_players_json(b, out, err);
}

bool remove(const std::string& turbo_output_utf8, std::string* err) {
    std::error_code ec;
    const fs::path o = rel_path(turbo_output_utf8, kOverrideRel);
    fs::remove(o, ec);
    if (fs::exists(o)) {
        if (err) *err = "cannot remove " + o.u8string() + ": " + ec.message();
        return false;
    }
    return lualoader::set_faces3d_opt_in(turbo_output_utf8, false, err);
}

ApplyResult apply(const std::string& turbo_output_utf8, const Table& t, const std::string& build_key,
                  const std::function<int64_t(int64_t)>& headclass) {
    ApplyResult r;
    std::string err;
    if (t.empty()) {
        r.ok = remove(turbo_output_utf8, &err);
        if (r.ok) {
            const std::string js = players_json(t);
            lualoader::write_file_atomic(rel_path(turbo_output_utf8, kPlayersRel).u8string(), js, &err);
        }
        r.message = r.ok ? "No player enrolled: the override and faces3d_on.txt were removed (restart the game)." : "Remove failed: " + err;
        return r;
    }
    const auto problems = validate(t, headclass ? headclass : std::function<int64_t(int64_t)>([](int64_t) { return int64_t(0); }));
    if (!problems.empty()) {
        r.message = "Nothing written: ";
        for (size_t i = 0; i < problems.size(); ++i) r.message += (i ? "; " : "") + problems[i];
        return r;
    }
    if (build_key.empty()) {
        r.message = "Nothing written: the game build is unknown (Turbo has not read FC27.exe yet).";
        return r;
    }
    std::string stock;
    const fs::path cache = rel_path(turbo_output_utf8, lualoader::kStockCacheRel);
    std::error_code ec;
    if (!fs::exists(cache, ec) || !read_file(cache, stock) || stock.empty()) {
        r.message = "Nothing written: the game's player_kit.lua has not been captured yet (turbo_output\\faces3d\\stock_player_kit.cache "
                    "is missing). Press 'Enable capture', restart the game once, then Apply again.";
        return r;
    }
    const std::string want = template_stock_md5(template_text());
    if (md5s(stock) != want) {
        r.message = "Nothing written: the captured player_kit.lua (md5 " + md5s(stock) + ") is not the one this Turbo knows (" + want +
                    "): game update? Delete turbo_output\\faces3d\\stock_player_kit.cache and restart to capture it again.";
        return r;
    }
    const std::string js = players_json(t);
    const Built b = generate_override(template_text(), stock, t, build_key, js);
    if (!b.ok) {
        r.message = "Nothing written: " + b.error;
        return r;
    }
    if (!lualoader::write_file_atomic(rel_path(turbo_output_utf8, kPlayersRel).u8string(), js, &err) ||
        !lualoader::write_file_atomic(rel_path(turbo_output_utf8, kOverrideRel).u8string(), b.bytes, &err)) {
        r.message = "Write failed: " + err;
        return r;
    }
    if (!lualoader::set_faces3d_opt_in(turbo_output_utf8, true, &err)) {
        r.message = "Override written, but faces3d_on.txt failed: " + err;
        return r;
    }
    r.ok = true;
    r.override_md5 = b.md5;
    r.table = table_hash(js);
    r.message = "Applied for " + std::to_string(t.size()) + " player(s): override md5 " + b.md5 + ", table " + r.table +
                ". Restart the game to see it (player_kit.lua is read only at game start).";
    return r;
}

}  // namespace turbo::faces3d
