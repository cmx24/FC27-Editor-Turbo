// FC 27 LE Turbo GUI - faces3d (core/faces3d.h). The generator is a byte-for-byte port of
// turbo_dev/research/c2_lua_loader/build_gated_v2.py build() / entries_lua() / parse_template().
#include "core/faces3d.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>

#include "core/faces3d_data.h"
#include "core/lua_loader.h"
#include "nlohmann/json.hpp"

namespace turbo::faces3d {

namespace fs = std::filesystem;
using lualoader::md5_hex;

const char* const kPlayersRel = "faces3d\\players.json";
const char* const kOverrideRel = "lua_override\\player_kit.lua";
const char* const kOptInRel = "faces3d_on.txt";
const char* const kAppliedDirRel = "faces3d\\applied";
const char* const kPreviewBackupRel = "faces3d\\preview_backup.json";
const char* const kPreviewFields[3] = {"hairtypecode", "facialhairtypecode", "hairstylecode"};

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
        if (e.contains("mode")) {
            const auto& m = e["mode"];
            if (m.is_string() && m.get<std::string>() == "follow") en.follow = true;
            else if (!(m.is_string() && m.get<std::string>() == "fixed"))
                return fail(it.key() + ": mode must be \"fixed\" or \"follow\", got " + m.dump());
        }
        for (auto f = e.begin(); f != e.end(); ++f) {
            const std::string& k = f.key();
            const auto& v = f.value();
            if (k == "mode") continue;
            if ((k == "hair" || k == "beard") && !en.follow) {
                if (v.is_null()) continue;
                if (!v.is_number_integer() || v.get<int64_t>() < 1)
                    return fail(it.key() + ": " + k + " must be a positive integer model id, got " + v.dump());
                (k == "hair" ? en.hair : en.beard) = v.get<int64_t>();
            } else if ((k == "hair_fallback" || k == "beard_fallback") && en.follow) {
                if (v.is_null()) continue;
                if (!v.is_number_integer() || v.get<int64_t>() < 1)
                    return fail(it.key() + ": " + k + " must be a positive integer model id, got " + v.dump());
                (k == "hair_fallback" ? en.hair_fallback : en.beard_fallback) = v.get<int64_t>();
            } else if (k == "parts" && en.follow) {
                bool h = false, b = false, bad = !v.is_array() || v.empty();
                if (!bad)
                    for (const auto& x : v) {
                        const std::string s = x.is_string() ? x.get<std::string>() : std::string();
                        if (s == "hair" && !h) h = true;
                        else if (s == "beard" && !b) b = true;
                        else bad = true;
                    }
                if (bad) return fail(it.key() + ": parts must be a non-empty list of \"hair\" / \"beard\", got " + v.dump());
                en.follow_hair = h, en.follow_beard = b;
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
                return fail(it.key() + ": unknown field \"" + k + "\" for " + (en.follow ? "follow" : "fixed") + " mode");
            }
        }
        if (!have_hc) en.hair_colour = colours;
        if (!have_bc) en.beard_colour = colours;
        if (en.follow) {
            for (const auto& [fb, followed, part, ok] :
                 {std::tuple<int64_t, bool, const char*, bool>{en.hair_fallback, en.follow_hair, "hair", hair_id_ok(en.hair_fallback)},
                  std::tuple<int64_t, bool, const char*, bool>{en.beard_fallback, en.follow_beard, "beard", beard_id_ok(en.beard_fallback)}}) {
                if (fb > 0 && !followed) return fail(it.key() + ": " + part + "_fallback given but " + part + " is not a follow part");
                if (fb > 0 && !ok)
                    return fail(it.key() + ": " + part + "_fallback " + std::to_string(fb) + " is not a verified generic model");
            }
        }
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
        if (e.follow) {
            f += "\"mode\": \"follow\", ";
            if (!(e.follow_hair && e.follow_beard)) f += std::string("\"parts\": [\"") + (e.follow_hair ? "hair" : "beard") + "\"], ";
            if (e.follow_hair && e.hair_fallback > 0) f += "\"hair_fallback\": " + std::to_string(e.hair_fallback) + ", ";
            if (e.follow_beard && e.beard_fallback > 0) f += "\"beard_fallback\": " + std::to_string(e.beard_fallback) + ", ";
        } else {
            if (e.hair > 0) f += "\"hair\": " + std::to_string(e.hair) + ", ";
            if (e.beard > 0) f += "\"beard\": " + std::to_string(e.beard) + ", ";
        }
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
        if (e.follow) {
            if (!e.follow_hair && !e.follow_beard) v.push_back(who + ": follow needs the hair or the beard part");
            if (e.hair_fallback > 0 && (!e.follow_hair || !hair_id_ok(e.hair_fallback)))
                v.push_back(who + ": hair fallback " + std::to_string(e.hair_fallback) + " is not a verified id of a followed part");
            if (e.beard_fallback > 0 && (!e.follow_beard || !beard_id_ok(e.beard_fallback)))
                v.push_back(who + ": beard fallback " + std::to_string(e.beard_fallback) + " is not a verified id of a followed part");
        } else {
            if (e.hair <= 0 && e.beard <= 0) v.push_back(who + ": choose a hair or a beard (or un-enrol him)");
            if (e.hair > 0 && !hair_id_ok(e.hair)) v.push_back(who + ": hair " + std::to_string(e.hair) + " is not a verified generic hair id");
            if (e.beard > 0 && !beard_id_ok(e.beard)) v.push_back(who + ": beard " + std::to_string(e.beard) + " is not a verified generic beard id");
        }
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
const char* template_v3_text() { return data::kTemplateV3; }
const char* template_v3_md5() { return data::kTemplateV3Md5; }
bool has_follow(const Table& t) {
    for (const auto& kv : t)
        if (kv.second.follow) return true;
    return false;
}
const char* template_for(const Table& t) { return has_follow(t) ? template_v3_text() : template_text(); }

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
    bool v3 = false;   // the gate block has --@@OKHAIR@@ / --@@OKBEARD@@ (build_gated_v3.py)
};

size_t count_of(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) ++n;
    return n;
}

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
    if (count_of(sp.gate, "--@@ENTRIES@@") != 1) {
        err = "template: gate block with exactly one --@@ENTRIES@@ marker required";
        return false;
    }
    const size_t oh = count_of(sp.gate, "--@@OKHAIR@@"), ob = count_of(sp.gate, "--@@OKBEARD@@");
    if (oh || ob) {
        if (oh != 1 || ob != 1) {
            err = "template: gate block needs exactly one --@@OKHAIR@@ and one --@@OKBEARD@@ marker";
            return false;
        }
        sp.v3 = true;
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

// build_gated_v3.py entries_lua(): fixed = v2's parts; follow = "beard = -1"[, "bf = N"], "hair = -1"[, "hf = N"]; then hc, bc;
// the comment is always "  -- <mode>[ <name>]"
bool entries_lua_v3(const Table& t, std::string& out, std::string& err) {
    std::vector<std::string> lines;
    for (const auto& [id, e] : t) {
        const std::string key = std::to_string(id);
        if (id <= 0) {
            err = "players: key " + key + " is not a player id";
            return false;
        }
        std::vector<std::string> parts;
        if (!e.follow) {
            if (e.hair < 0 || e.beard < 0) {
                err = "player " + key + ": model ids must be positive";
                return false;
            }
            if (e.hair == 0 && e.beard == 0) continue;
            if (e.beard) parts.push_back("beard = " + std::to_string(e.beard));
            if (e.hair) parts.push_back("hair = " + std::to_string(e.hair));
        } else {
            if (!e.follow_hair && !e.follow_beard) {
                err = "player " + key + ": parts must be a non-empty list of 'hair'/'beard'";
                return false;
            }
            struct P {
                const char* part;
                const char* shortname;
                bool followed;
                int64_t fb;
                bool ok;
            };
            for (const P& p : {P{"beard", "bf", e.follow_beard, e.beard_fallback, beard_id_ok(e.beard_fallback)},
                               P{"hair", "hf", e.follow_hair, e.hair_fallback, hair_id_ok(e.hair_fallback)}}) {
                if (p.fb < 0) {
                    err = "player " + key + ": " + p.part + "_fallback must be a positive integer model id";
                    return false;
                }
                if (!p.followed) {
                    if (p.fb > 0) {
                        err = "player " + key + ": " + p.part + "_fallback given but " + p.part + " is not a follow part";
                        return false;
                    }
                    continue;
                }
                if (p.fb > 0 && !p.ok) {
                    err = "player " + key + ": " + p.part + "_fallback " + std::to_string(p.fb) + " is not a verified generic model (allow-list)";
                    return false;
                }
                parts.push_back(std::string(p.part) + " = -1");
                if (p.fb > 0) parts.push_back(std::string(p.shortname) + " = " + std::to_string(p.fb));
            }
        }
        parts.push_back(std::string("hc = ") + (e.hair_colour ? "true" : "false"));
        parts.push_back(std::string("bc = ") + (e.beard_colour ? "true" : "false"));
        std::string joined;
        for (size_t i = 0; i < parts.size(); ++i) joined += (i ? ", " : "") + parts[i];
        const std::string name = sanitize_name(e.name);
        lines.push_back("\t\t[" + key + "] = { " + joined + " },  -- " + (e.follow ? "follow" : "fixed") + (name.empty() ? "" : " " + name));
    }
    out.clear();
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return true;
}

// build_gated_v3.py ranges_lua(): sorted inclusive ranges "lo, hi", 12 per line, "\t\t" + ", ".join + ","
std::string ranges_lua(const std::vector<int>& ids0) {
    std::vector<int> ids = ids0;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<std::pair<int, int>> r;
    for (int v : ids) {
        if (!r.empty() && v == r.back().second + 1) r.back().second = v;
        else r.emplace_back(v, v);
    }
    std::string out;
    for (size_t i = 0; i < r.size(); i += 12) {
        if (i) out += "\n";
        out += "\t\t";
        for (size_t k = i; k < std::min(r.size(), i + 12); ++k)
            out += (k > i ? ", " : "") + std::to_string(r[k].first) + ", " + std::to_string(r[k].second);
        out += ",";
    }
    return out;
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
    if (!sp.v3 && has_follow(t)) {
        r.error = "follow entries need the v3 template";
        return r;
    }
    if (!(sp.v3 ? entries_lua_v3(t, entries, r.error) : entries_lua(t, entries, r.error))) return r;

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
    // v2 tests the table (`if players`), v3 the entries text (`if ent`): a table of dropped entries differs between the two
    std::string gate = replace_all(sp.gate, "--@@ENTRIES@@\n", (sp.v3 ? !entries.empty() : !t.empty()) ? entries + "\n" : std::string());
    if (sp.v3) {
        gate = replace_all(gate, "--@@OKHAIR@@\n", ranges_lua(ok_hair_ids()) + "\n");
        gate = replace_all(gate, "--@@OKBEARD@@\n", ranges_lua(ok_beard_ids()) + "\n");
    }
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
    const char* tmpl = template_for(t);
    const std::string want = template_stock_md5(tmpl);
    if (md5s(stock) != want) {
        r.message = "Nothing written: the captured player_kit.lua (md5 " + md5s(stock) + ") is not the one this Turbo knows (" + want +
                    "): game update? Delete turbo_output\\faces3d\\stock_player_kit.cache and restart to capture it again.";
        return r;
    }
    const std::string js = players_json(t);
    const Built b = generate_override(tmpl, stock, t, build_key, js);
    if (!b.ok) {
        r.message = "Nothing written: " + b.error;
        return r;
    }
    // the snapshot first: a game start that serves this override can then tell which list it runs (running_table)
    const fs::path snap = rel_path(turbo_output_utf8, kAppliedDirRel) / (table_hash(js) + ".json");
    if (!lualoader::write_file_atomic(snap.u8string(), js, &err) ||
        !lualoader::write_file_atomic(rel_path(turbo_output_utf8, kPlayersRel).u8string(), js, &err) ||
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
    r.message = "Applied for " + std::to_string(t.size()) + " player(s) (gate " + (has_follow(t) ? "v3" : "v2") + "): override md5 " +
                b.md5 + ", table " + r.table + ". Restart the game to see it (player_kit.lua is read only at game start).";
    return r;
}

// ---------------------------------------------------------------- the running list
bool parse_gate_entries(const std::string& bytes, Table& out) {
    const size_t a = bytes.find("TURBO PER-PLAYER FACE GATE v");
    if (a == std::string::npos) return false;
    size_t b = bytes.find("END TURBO PER-PLAYER FACE GATE", a);
    if (b == std::string::npos) return false;
    Table t;
    size_t p = a;
    while (p < b) {
        size_t e = bytes.find('\n', p);
        if (e == std::string::npos || e > b) e = b;
        std::string line = bytes.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 3, "\t\t[") != 0) continue;
        const size_t close = line.find("] = { ");
        const size_t end = line.find(" },", close == std::string::npos ? 0 : close);
        if (close == std::string::npos || end == std::string::npos) continue;
        int64_t id = 0;
        if (!positive_key(line.substr(3, close - 3), id)) continue;
        Entry en;
        en.follow_hair = en.follow_beard = false;
        bool have_hair = false, have_beard = false;
        for (const std::string& kv : split(line.substr(close + 6, end - (close + 6)), ", ")) {
            const auto eq = kv.find(" = ");
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq), v = kv.substr(eq + 3);
            long long n = 0;
            if (k == "hc" || k == "bc") (k == "hc" ? en.hair_colour : en.beard_colour) = v == "true";
            else if (!to_int(v, n)) continue;
            else if (k == "hair") have_hair = true, en.hair = n;
            else if (k == "beard") have_beard = true, en.beard = n;
            else if (k == "hf") en.hair_fallback = n;
            else if (k == "bf") en.beard_fallback = n;
        }
        if (have_hair && en.hair == -1) en.follow = en.follow_hair = true, en.hair = 0;
        if (have_beard && en.beard == -1) en.follow = en.follow_beard = true, en.beard = 0;
        if (!en.follow) en.follow_hair = en.follow_beard = true;  // the defaults (unused for fixed)
        const size_t c = line.find("  -- ", end);
        if (c != std::string::npos) {
            std::string nm = line.substr(c + 5);
            for (const char* m : {"follow ", "fixed "})
                if (nm.compare(0, std::strlen(m), m) == 0) nm = nm.substr(std::strlen(m));
            if (nm != "follow" && nm != "fixed") en.name = nm;
        }
        t[id] = en;
    }
    out = std::move(t);
    return true;
}

bool running_table(const std::string& turbo_output_utf8, const std::string& header_table, Table& out, std::string* why) {
    if (header_table.size() != 12) {
        if (why) *why = "the override of this game start has no table hash";
        return false;
    }
    std::string b, err;
    const fs::path snap = rel_path(turbo_output_utf8, kAppliedDirRel) / (header_table + ".json");
    if (read_file(snap, b) && table_hash(b) == header_table && parse_players_json(b, out, &err)) return true;
    const fs::path ov = rel_path(turbo_output_utf8, kOverrideRel);
    if (read_file(ov, b)) {
        const lualoader::OverrideHeader h = lualoader::parse_override_header(b);
        if (h.ok && h.table == header_table && parse_gate_entries(b, out)) return true;
    }
    if (why)
        *why = "Turbo cannot tell which players this game start runs (no faces3d\\applied\\" + header_table +
               ".json, and the override on disk was changed since): restart the game";
    return false;
}

LiveGate live_gate(const lualoader::Faces3dStatus* st, const Table* running, int64_t pid) {
    LiveGate g;
    auto no = [&](const std::string& w) {
        g.why = w;
        return g;
    };
    if (!st) return no("no game hooks here (Turbo is not running inside the game)");
    if (!st->opted_in) return no("the 3D hair hook was off at this game start (faces3d_on.txt was not there): Apply, then restart the game");
    if (!st->installed) return no("the 3D hair hook is not installed this session (see the Status tab)");
    if (st->killed) return no("a kill switch is present (faces3d_off.txt / hook_lua_load_off.txt): the gate is off");
    if (st->legacy_refused) return no("mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua exists: Turbo refused the override this session");
    if (!st->substitute) return no("the hook runs log-only this session (no override was served)");
    if (!st->header_ok) return no("no Turbo override was there at this game start: Apply, then restart the game");
    if (!st->build_key_ok) return no("the override was made for another game build: Apply again, then restart");
    if (!st->stock_matches_header) return no("the override was made from another player_kit.lua than this game's (stale): Apply again, then restart");
    if (st->substituted <= 0)
        return no(st->fallbacks > 0 ? "the override failed to compile in the game (fallbacks " + std::to_string(st->fallbacks) + "): the stock file runs"
                                    : "the game has not loaded the override this session (substituted 0)");
    if (!running) return no("Turbo cannot tell which players this game start runs: restart the game");
    const auto it = running->find(pid);
    if (it == running->end())
        return no("he was not on the list when the game started: enrolling a player takes effect after a game restart (Apply, restart, then preview)");
    g.member = true;
    if (!it->second.follow)
        return no("he has a fixed look at this game start: the preview needs 'Follow his database codes' (tick it, Apply, restart once)");
    g.follow_hair = it->second.follow_hair;
    g.follow_beard = it->second.follow_beard;
    g.active = true;
    g.why = std::string("live: this game start follows his ") +
            (g.follow_hair && g.follow_beard ? "hair and beard codes" : g.follow_hair ? "hair code (beard stays as it is)" : "beard code (hair stays as it is)");
    return g;
}

// ---------------------------------------------------------------- preview backup
bool load_preview_backup(const std::string& turbo_output_utf8, PreviewBackup& out, std::string* err) {
    out.clear();
    const fs::path p = rel_path(turbo_output_utf8, kPreviewBackupRel);
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::string b;
    if (!read_file(p, b)) {
        if (err) *err = "cannot read " + p.u8string();
        return false;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(b);
        if (!j.is_object() || !j.contains("players") || !j["players"].is_object()) throw std::runtime_error("no \"players\" object");
        for (auto it = j["players"].begin(); it != j["players"].end(); ++it) {
            int64_t id = 0;
            if (!positive_key(it.key(), id)) continue;
            PreviewRecord r;
            const auto& e = it.value();
            if (e.contains("name") && e["name"].is_string()) r.name = e["name"].get<std::string>();
            for (const char* which : {"original", "written"}) {
                if (!e.contains(which) || !e[which].is_object()) continue;
                auto& m = std::string(which) == "original" ? r.original : r.written;
                for (auto f = e[which].begin(); f != e[which].end(); ++f)
                    if (f.value().is_number_integer()) m[f.key()] = f.value().get<int64_t>();
            }
            if (!r.original.empty()) out[id] = r;
        }
    } catch (const std::exception& e) {
        if (err) *err = "preview_backup.json: " + std::string(e.what());
        return false;
    }
    return true;
}

bool save_preview_backup(const std::string& turbo_output_utf8, const PreviewBackup& b, std::string* err) {
    const fs::path p = rel_path(turbo_output_utf8, kPreviewBackupRel);
    if (b.empty()) {
        std::error_code ec;
        fs::remove(p, ec);
        if (fs::exists(p)) {
            if (err) *err = "cannot remove " + p.u8string();
            return false;
        }
        return true;
    }
    nlohmann::json j;
    j["note"] = "Turbo 3D hair preview: the database codes a preview replaced (Revert writes 'original' back when the codes still equal 'written')";
    nlohmann::json pl = nlohmann::json::object();
    for (const auto& [id, r] : b) {
        nlohmann::json e;
        e["name"] = r.name;
        e["original"] = r.original;
        e["written"] = r.written;
        pl[std::to_string(id)] = e;
    }
    j["players"] = pl;
    return lualoader::write_file_atomic(p.u8string(), j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n", err);
}

std::map<std::string, int64_t> preview_writes(int64_t hair, int64_t beard, const std::map<std::string, int64_t>& cur) {
    std::map<std::string, int64_t> w;
    auto now = [&](const char* f) {
        const auto it = cur.find(f);
        return it == cur.end() ? int64_t(-1) : it->second;
    };
    if (hair > 0 && now("hairtypecode") != hair) w["hairtypecode"] = hair;
    if (beard > 0 && now("facialhairtypecode") != beard) w["facialhairtypecode"] = beard;
    if ((hair > 0 || beard > 0) && cur.count("hairstylecode") && now("hairstylecode") != 0) w["hairstylecode"] = 0;
    return w;
}

}  // namespace turbo::faces3d
