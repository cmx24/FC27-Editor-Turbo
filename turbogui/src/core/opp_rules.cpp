// FC 27 LE Turbo GUI - the opposition rules (see opp_rules.h)
#include "opp_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "nlohmann/json.hpp"
#include "sliders.h"
#include "tactic_profiles.h"  // read_text_file, write_text_atomic

namespace turbo {

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

const char* const kFamilyKeys[kFamilyCount] = {"gegenpress", "low_block", "possession", "wing_play", "direct", "balanced"};

const char* family_label(size_t i) {
    static const char* const l[kFamilyCount] = {"Gegenpress", "Low block counter", "Possession", "Wing play", "Direct", "Balanced"};
    return i < kFamilyCount ? l[i] : "";
}

int family_index(const std::string& key) {
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (key == kFamilyKeys[i]) return static_cast<int>(i);
    return -1;
}

const std::vector<std::string>& known_facts() {
    static const std::vector<std::string> f = [] {
        std::vector<std::string> v = {"strength_ratio", "user_ovr", "opp_ovr", "reputation", "form",   "age",    "pace",
                                      "passing",        "aerial",   "home",    "mentality",  "n_def",  "n_mid",  "n_att", "n_wide"};
        for (const char* k : kFamilyKeys) v.push_back(std::string("family_") + k);
        return v;
    }();
    return f;
}

namespace {

bool known_fact(const std::string& f) {
    const auto& k = known_facts();
    return std::find(k.begin(), k.end(), f) != k.end();
}

bool known_op(const std::string& o) { return o == "<" || o == "<=" || o == ">" || o == ">=" || o == "==" || o == "!="; }
bool known_action(const std::string& o) { return o == "add" || o == "set" || o == "boost_family" || o == "scale_amplitude"; }

// the sliders a rule may change: any team slider, and the opposition sliders that are written to the game (injury, difficulty offsets)
bool valid_target(const std::string& key) {
    const SliderDef* d = find_slider(key);
    if (!d) return false;
    if (d->scope == SliderScope::Team) return true;
    return d->scope == SliderScope::Opposition && d->status == SliderStatus::Live;
}

const char* kBuiltinRulesJson = R"JSON({
 "turbo_opp_rules": 1,
 "rules": [
  {"id": "underdog_low_block", "priority": 60, "note": "A clearly weaker side drops back and goes direct.",
   "when": {"all": [{"fact": "strength_ratio", "op": "<", "value": 0.85}]},
   "then": [{"op": "boost_family", "target": "low_block", "value": 0.35}, {"op": "add", "target": "team.line_depth", "value": -4}]},
  {"id": "home_favourite_press", "priority": 50, "note": "A stronger side at home presses higher.",
   "when": {"all": [{"fact": "strength_ratio", "op": ">", "value": 1.15}, {"fact": "home", "op": "==", "value": 1}]},
   "then": [{"op": "boost_family", "target": "gegenpress", "value": 0.3}, {"op": "add", "target": "team.engagement_height", "value": 3}]},
  {"id": "away_caution", "priority": 40, "note": "Away from home a side of equal strength is a little more careful.",
   "when": {"all": [{"fact": "home", "op": "==", "value": 0}, {"fact": "strength_ratio", "op": ">=", "value": 0.9}, {"fact": "strength_ratio", "op": "<=", "value": 1.1}]},
   "then": [{"op": "add", "target": "team.tempo", "value": -3}, {"op": "add", "target": "team.line_depth", "value": -2}]},
  {"id": "tall_target_direct", "priority": 30, "note": "Aerial strength with modest passing: play it long.",
   "when": {"all": [{"fact": "aerial", "op": "==", "value": 1}, {"fact": "passing", "op": "<", "value": 62}]},
   "then": [{"op": "boost_family", "target": "direct", "value": 0.3}]},
  {"id": "weak_side_stay_recognisable", "priority": 20, "note": "The weakest sides change less, so they stay recognisable.",
   "when": {"all": [{"fact": "strength_ratio", "op": "<", "value": 0.7}]},
   "then": [{"op": "scale_amplitude", "value": 0.8}]}
 ]
})JSON";

std::string fnum(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%+g", v);
    return b;
}

void parse_conds(const ojson& arr, std::vector<RuleCond>& out, bool& ok) {
    if (!arr.is_array()) {
        ok = false;
        return;
    }
    for (const ojson& c : arr) {
        RuleCond rc;
        if (!c.is_object() || !c.contains("fact") || !c["fact"].is_string() || !c.contains("op") || !c["op"].is_string() || !c.contains("value") ||
            !c["value"].is_number()) {
            ok = false;
            return;
        }
        rc.fact = c["fact"].get<std::string>();
        rc.op = c["op"].get<std::string>();
        rc.value = c["value"].get<double>();
        if (!known_fact(rc.fact) || !known_op(rc.op) || !std::isfinite(rc.value)) {
            ok = false;
            return;
        }
        out.push_back(rc);
    }
}

bool parse_rule(const ojson& e, OppRule& r) {
    if (!e.is_object() || !e.contains("id") || !e["id"].is_string() || e["id"].get<std::string>().empty()) return false;
    r = OppRule{};
    r.id = e["id"].get<std::string>();
    if (e.contains("note") && e["note"].is_string()) r.note = e["note"].get<std::string>();
    if (e.contains("priority")) {
        if (!e["priority"].is_number_integer()) return false;
        r.priority = static_cast<int>(std::max<int64_t>(-100000, std::min<int64_t>(100000, e["priority"].get<int64_t>())));
    }
    if (e.contains("enabled") && e["enabled"].is_boolean()) r.enabled = e["enabled"].get<bool>();
    if (e.contains("stop") && e["stop"].is_boolean()) r.stop = e["stop"].get<bool>();
    bool ok = true;
    if (e.contains("when")) {
        const ojson& w = e["when"];
        if (w.is_array()) {
            parse_conds(w, r.all, ok);
        } else if (w.is_object()) {
            if (w.contains("all")) parse_conds(w["all"], r.all, ok);
            if (w.contains("any")) parse_conds(w["any"], r.any, ok);
        } else {
            ok = false;
        }
    }
    if (!ok) return false;
    if (!e.contains("then") || !e["then"].is_array()) return false;
    for (const ojson& a : e["then"]) {
        if (!a.is_object() || !a.contains("op") || !a["op"].is_string() || !a.contains("value") || !a["value"].is_number()) continue;
        RuleAction ra;
        ra.op = a["op"].get<std::string>();
        ra.value = a["value"].get<double>();
        if (a.contains("target") && a["target"].is_string()) ra.target = a["target"].get<std::string>();
        if (!known_action(ra.op) || !std::isfinite(ra.value)) continue;
        if ((ra.op == "add" || ra.op == "set") && !valid_target(ra.target)) continue;
        if (ra.op == "boost_family" && family_index(ra.target) < 0) continue;
        if (ra.op == "scale_amplitude" && !(ra.value > 0.0 && ra.value <= 5.0)) continue;
        r.then.push_back(ra);
    }
    return !r.then.empty();
}

ojson rule_to_json(const OppRule& r) {
    ojson j = ojson::object();
    j["id"] = r.id;
    j["priority"] = r.priority;
    j["enabled"] = r.enabled;
    j["stop"] = r.stop;
    j["note"] = r.note;
    auto conds = [](const std::vector<RuleCond>& v) {
        ojson a = ojson::array();
        for (const RuleCond& c : v) a.push_back({{"fact", c.fact}, {"op", c.op}, {"value", c.value}});
        return a;
    };
    ojson w = ojson::object();
    w["all"] = conds(r.all);
    w["any"] = conds(r.any);
    j["when"] = w;
    ojson t = ojson::array();
    for (const RuleAction& a : r.then) {
        ojson o = ojson::object();
        o["op"] = a.op;
        if (!a.target.empty()) o["target"] = a.target;
        o["value"] = a.value;
        t.push_back(o);
    }
    j["then"] = t;
    return j;
}

}  // namespace

const RuleSet& builtin_rules() {
    static const RuleSet rs = [] {
        RuleSet r;
        size_t dropped = 0;
        std::string err;
        parse_opp_rules_json(kBuiltinRulesJson, r, &err, &dropped);
        for (OppRule& x : r.rules) x.builtin = true;
        r.read_only = false;
        return r;
    }();
    return rs;
}

RuleSet merge_rules(const RuleSet& builtin, const RuleSet& user) {
    RuleSet out = builtin;
    for (const OppRule& u : user.rules) {
        bool replaced = false;
        for (OppRule& b : out.rules)
            if (b.id == u.id) {
                b = u;
                b.builtin = false;
                replaced = true;
                break;
            }
        if (!replaced) out.rules.push_back(u);
    }
    return out;
}

bool eval_condition(const RuleCond& c, const FactSet& facts) {
    auto it = facts.find(c.fact);
    if (it == facts.end()) return false;
    const double v = it->second;
    if (c.op == "<") return v < c.value;
    if (c.op == "<=") return v <= c.value;
    if (c.op == ">") return v > c.value;
    if (c.op == ">=") return v >= c.value;
    if (c.op == "==") return std::fabs(v - c.value) < 1e-9;
    if (c.op == "!=") return std::fabs(v - c.value) >= 1e-9;
    return false;
}

bool rule_matches(const OppRule& r, const FactSet& facts) {
    for (const RuleCond& c : r.all)
        if (!eval_condition(c, facts)) return false;
    if (!r.any.empty()) {
        bool one = false;
        for (const RuleCond& c : r.any)
            if (eval_condition(c, facts)) {
                one = true;
                break;
            }
        if (!one) return false;
    }
    return true;
}

RuleOutcome evaluate_rules(const RuleSet& rs, const FactSet& facts) {
    RuleOutcome out;
    std::vector<const OppRule*> order;
    for (const OppRule& r : rs.rules)
        if (r.enabled) order.push_back(&r);
    std::stable_sort(order.begin(), order.end(), [](const OppRule* a, const OppRule* b) {
        if (a->priority != b->priority) return a->priority > b->priority;
        return a->id < b->id;
    });
    std::set<std::string> touched;  // keys a rule of higher priority already changed
    for (const OppRule* r : order) {
        if (!rule_matches(*r, facts)) continue;
        RuleHit hit;
        hit.id = r->id;
        hit.priority = r->priority;
        for (const RuleAction& a : r->then) {
            const SliderDef* d = find_slider(a.target);
            const std::string name = d ? d->label : a.target;
            if (a.op == "boost_family") {
                out.family_boost[family_index(a.target)] += a.value;
                hit.effects.push_back(std::string(family_label(static_cast<size_t>(family_index(a.target)))) + " score " + fnum(a.value));
            } else if (a.op == "scale_amplitude") {
                out.amplitude_scale *= a.value;
                char b[48];
                std::snprintf(b, sizeof(b), "all changes x%g", a.value);
                hit.effects.push_back(b);
            } else if (a.op == "add") {
                if (out.set.count(a.target)) {
                    hit.effects.push_back("ignored (a higher-priority rule pinned it): " + name + " " + fnum(a.value));
                } else {
                    out.add[a.target] += a.value;
                    touched.insert(a.target);
                    hit.effects.push_back(name + " " + fnum(a.value));
                }
            } else if (a.op == "set") {
                if (touched.count(a.target)) {
                    hit.effects.push_back("ignored (a higher-priority rule changed it): " + name + " = " + fnum(a.value));
                } else {
                    out.set[a.target] = a.value;
                    touched.insert(a.target);
                    hit.effects.push_back(name + " pinned to " + fnum(a.value));
                }
            }
        }
        out.hits.push_back(hit);
        if (r->stop) break;
    }
    return out;
}

std::string opp_rules_json(const RuleSet& user) {
    ojson j = ojson::object();
    j["turbo_opp_rules"] = kOppRulesVersion;
    j["note"] = "Opposition rules. A rule with the same id as a built-in one replaces it; \"enabled\": false switches a built-in off.";
    ojson arr = ojson::array();
    for (const OppRule& r : user.rules) arr.push_back(rule_to_json(r));
    j["rules"] = arr;
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool parse_opp_rules_json(const std::string& text, RuleSet& out, std::string* err, size_t* dropped) {
    out = RuleSet{};
    if (dropped) *dropped = 0;
    size_t bad = 0;
    try {
        ojson j = ojson::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        if (!j.contains("turbo_opp_rules") || !j["turbo_opp_rules"].is_number_integer() || j["turbo_opp_rules"].get<int64_t>() < 1) {
            if (err) *err = "not a Turbo opposition rules file (no \"turbo_opp_rules\": 1)";
            return false;
        }
        const int64_t ver = j["turbo_opp_rules"].get<int64_t>();
        out.version = static_cast<int>(std::min<int64_t>(ver, 1000000));
        out.read_only = ver > kOppRulesVersion;
        std::set<std::string> ids;
        if (j.contains("rules") && j["rules"].is_array()) {
            for (const ojson& e : j["rules"]) {
                OppRule r;
                if (!parse_rule(e, r) || !ids.insert(r.id).second) {
                    ++bad;
                    continue;
                }
                out.rules.push_back(std::move(r));
            }
        }
    } catch (const std::exception& e) {
        out = RuleSet{};
        if (err) *err = std::string("cannot read the rules: ") + e.what();
        return false;
    }
    if (dropped) *dropped = bad;
    return true;
}

fs::path opp_rules_path(const fs::path& le_root) { return le_root / "turbo_output" / "opp_rules.json"; }

fs::path opp_rules_unreadable_path(const fs::path& p) { return p.parent_path() / "opp_rules.unreadable.json"; }

bool load_opp_rules(const fs::path& p, RuleSet& out, std::string* err, size_t* dropped) {
    out = RuleSet{};
    if (dropped) *dropped = 0;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::string text, why;
    if (!read_text_file(p, text, err)) return false;
    if (!parse_opp_rules_json(text, out, &why, dropped)) {
        if (err) *err = p.filename().string() + ": " + why;
        return false;
    }
    return true;
}

bool save_opp_rules(const fs::path& p, const RuleSet& user, std::string* err, bool set_aside) {
    if (user.read_only) {
        if (err) *err = "the rules file was written by a newer Turbo and is read-only here";
        return false;
    }
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (set_aside && fs::exists(p, ec)) {
        const fs::path keep = opp_rules_unreadable_path(p);
        fs::remove(keep, ec);
        ec.clear();
        fs::rename(p, keep, ec);
        if (ec) {
            if (err) *err = "cannot set the unreadable " + p.filename().string() + " aside: " + ec.message();
            return false;
        }
    }
    return write_text_atomic(p, opp_rules_json(user), err);
}

}  // namespace turbo
