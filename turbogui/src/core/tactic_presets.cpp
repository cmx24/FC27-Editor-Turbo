// FC 27 LE Turbo GUI - Tactic presets (see tactic_presets.h).
#include "tactic_presets.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "game_settings.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace tpreset {

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;
using tsheet::approach_text;
using tsheet::build_up_text;

namespace {

bool printable_text(const std::string& s, size_t max_len) {
    if (s.size() > max_len) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}

bool plain_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

std::string tactic_problem(const TacticBlock& t) {
    if (!t.present) return "";
    if (!printable_text(t.record_name, 32)) return "tactic: record_name is not a short plain text";
    if (t.formation < 0 || t.formation > 100000 || t.source_formation < 0 || t.source_formation > 100000) return "tactic: formation id out of range";
    if (t.line_height < tsheet::kLineMin || t.line_height > tsheet::kLineMax)
        return "tactic: line_height " + std::to_string(t.line_height) + " is outside " + std::to_string(tsheet::kLineMin) + ".." + std::to_string(tsheet::kLineMax);
    if (t.build_up < tsheet::kBuildUpMin || t.build_up > tsheet::kBuildUpMax)
        return "tactic: build_up " + std::to_string(t.build_up) + " is outside 0..3";
    if (t.approach < tsheet::kApproachMin || t.approach > tsheet::kApproachMax)
        return "tactic: approach " + std::to_string(t.approach) + " is outside 0..3";
    for (int s = 0; s < kSlots; ++s) {
        const SlotRec& r = t.slots[size_t(s)];
        const std::string at = "tactic: slot " + std::to_string(s) + " ";
        if (r.position < -1 || r.position > tsheet::kMaxPosition) return at + "position " + std::to_string(r.position) + " is outside -1.." + std::to_string(tsheet::kMaxPosition);
        if (r.role_group < 0 || r.role_group > tsheet::kRoleWordMax >> 12) return at + "role_group " + std::to_string(r.role_group) + " is out of range";
        if (r.role_id < 0 || r.role_id > tsheet::kRoleIdMax) return at + "role_id " + std::to_string(r.role_id) + " is outside 0.." + std::to_string(tsheet::kRoleIdMax);
        if (r.focus < 0 || r.focus > tsheet::kFocusMax) return at + "focus " + std::to_string(r.focus) + " is outside 0.." + std::to_string(tsheet::kFocusMax);
    }
    return "";
}

std::string settings_problem(const SettingsBlock& s) {
    if (!s.present) return "";
    if (s.layout != gsettings::layout_name())
        return "settings: layout \"" + s.layout + "\" is not the one this Turbo knows (" + gsettings::layout_name() + ")";
    if (!printable_text(s.map_build, 32)) return "settings: slider_map_build is not a short plain text";
    if (s.values.empty()) return "settings: no values";
    if (s.values.size() > 512) return "settings: too many values";
    for (const auto& kv : s.values) {
        if (!plain_id(kv.first)) return "settings: \"" + kv.first + "\" is not a slider id";
        if (kv.second < 0 || kv.second > 100) return "settings: " + kv.first + " = " + std::to_string(kv.second) + " is outside 0..100";
    }
    return "";
}

ojson block_json(const TacticBlock& t) {
    ojson j = ojson::object();
    j["record_name"] = t.record_name;
    j["formation"] = t.formation;
    j["source_formation"] = t.source_formation;
    j["line_height"] = t.line_height;
    j["build_up"] = t.build_up;
    j["approach"] = t.approach;
    ojson sl = ojson::array();
    for (const SlotRec& r : t.slots) {
        ojson o = ojson::object();
        o["position"] = r.position;
        o["role_group"] = r.role_group;
        o["role_id"] = r.role_id;
        o["focus"] = r.focus;
        sl.push_back(o);
    }
    j["slots"] = sl;
    return j;
}

bool get_int(const ojson& o, const char* key, int& out, std::string* err, const std::string& where) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_number_integer()) {
        if (err) *err = where + std::string(key) + " is missing or not a whole number";
        return false;
    }
    const long long v = it->get<long long>();
    if (v < -1000000 || v > 1000000) {
        if (err) *err = where + std::string(key) + " is out of range";
        return false;
    }
    out = int(v);
    return true;
}

bool block_parse(const ojson& j, TacticBlock& t, std::string* err) {
    if (!j.is_object()) {
        if (err) *err = "tactic is not an object";
        return false;
    }
    TacticBlock b;
    b.present = true;
    const std::string w = "tactic: ";
    auto rn = j.find("record_name");
    if (rn != j.end() && rn->is_string()) b.record_name = rn->get<std::string>();
    else if (rn != j.end()) {
        if (err) *err = w + "record_name is not text";
        return false;
    }
    if (!get_int(j, "formation", b.formation, err, w) || !get_int(j, "source_formation", b.source_formation, err, w) ||
        !get_int(j, "line_height", b.line_height, err, w) || !get_int(j, "build_up", b.build_up, err, w) || !get_int(j, "approach", b.approach, err, w))
        return false;
    auto sl = j.find("slots");
    if (sl == j.end() || !sl->is_array() || sl->size() != size_t(kSlots)) {
        if (err) *err = w + "slots must be a list of exactly " + std::to_string(kSlots);
        return false;
    }
    for (int s = 0; s < kSlots; ++s) {
        const ojson& o = (*sl)[size_t(s)];
        const std::string ws = w + "slot " + std::to_string(s) + " ";
        if (!o.is_object()) {
            if (err) *err = ws + "is not an object";
            return false;
        }
        SlotRec& r = b.slots[size_t(s)];
        if (!get_int(o, "position", r.position, err, ws) || !get_int(o, "role_group", r.role_group, err, ws) || !get_int(o, "role_id", r.role_id, err, ws) ||
            !get_int(o, "focus", r.focus, err, ws))
            return false;
    }
    t = b;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_small_file(const fs::path& p, std::string& out) {
    std::error_code ec;
    const auto sz = fs::file_size(p, ec);
    if (ec || sz > kMaxFileBytes) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

bool TacticBlock::same_values(const TacticBlock& o) const {
    if (line_height != o.line_height || build_up != o.build_up || approach != o.approach) return false;
    for (int s = 0; s < kSlots; ++s)
        if (slots[size_t(s)].role_id != o.slots[size_t(s)].role_id || slots[size_t(s)].focus != o.slots[size_t(s)].focus) return false;
    return true;
}

// ------------------------------------------------------------------------------------------------ validation and JSON
std::string validate(const Preset& p) {
    if (p.schema > kSchema) return "written by a newer Turbo (schema " + std::to_string(p.schema) + ", this one reads " + std::to_string(kSchema) + ")";
    if (p.schema != kSchema) return "unknown schema " + std::to_string(p.schema);
    if (p.name.empty() || !printable_text(p.name, kMaxNameLen)) return "the name is empty or not a plain text of at most " + std::to_string(kMaxNameLen) + " characters";
    if (!printable_text(p.note, kMaxNoteLen)) return "the note is longer than " + std::to_string(kMaxNoteLen) + " characters or holds control characters";
    if (!printable_text(p.created, 40)) return "created is not a short plain text";
    if (p.game_build != build())
        return "made for game build \"" + p.game_build + "\"; this Turbo knows only " + build() + " (the layouts it writes were verified on that build)";
    if (!p.tactic.present && !p.settings.present && !p.setpieces.present) return "holds neither a tactic, settings nor set pieces";
    std::string why = tactic_problem(p.tactic);
    if (why.empty()) why = settings_problem(p.settings);
    if (why.empty()) why = setpiece_problem(p.setpieces);
    return why;
}

// ------------------------------------------------------------------------------------------------ set pieces
const std::vector<SetPieceField>& setpiece_fields() {
    static const std::vector<SetPieceField> f = {
        {"captainid", "Captain"},
        {"penaltytakerid", "Penalty taker"},
        {"leftfreekicktakerid", "Free kick taker (left)"},
        {"rightfreekicktakerid", "Free kick taker (right)"},
        {"longkicktakerid", "Long free kick taker"},
        {"leftcornerkicktakerid", "Corner taker (left)"},
        {"rightcornerkicktakerid", "Corner taker (right)"},
        {"throwerleft", "Throw-in taker (left)"},
        {"throwerright", "Throw-in taker (right)"},
        {"cksupport1", "Corner support 1"},
        {"cksupport2", "Corner support 2"},
        {"cksupport3", "Corner support 3"},
        {"cksupport4", "Corner support 4"},
        {"cksupport5", "Corner support 5"},
        {"cksupport6", "Corner support 6"},
        {"cksupport7", "Corner support 7"},
        {"cksupport8", "Corner support 8"},
        {"cksupport9", "Corner support 9"},
        {"customsub0in", "Pre-set substitution 1: on"},
        {"customsub0out", "Pre-set substitution 1: off"},
        {"customsub1in", "Pre-set substitution 2: on"},
        {"customsub1out", "Pre-set substitution 2: off"},
        {"customsub2in", "Pre-set substitution 3: on"},
        {"customsub2out", "Pre-set substitution 3: off"},
    };
    return f;
}

namespace {
const SetPieceField* setpiece_field(const std::string& n) {
    for (const SetPieceField& f : setpiece_fields())
        if (n == f.name) return &f;
    return nullptr;
}
constexpr int kMaxPlayerId = 500000;
}  // namespace

std::string setpiece_problem(const SetPieceBlock& s) {
    if (!s.present) return "";
    if (s.team < 1 || s.team > 200000) return "set pieces: team " + std::to_string(s.team) + " is outside 1..200000";
    if (s.values.empty()) return "set pieces: no values";
    for (const auto& kv : s.values) {
        if (!setpiece_field(kv.first)) return "set pieces: \"" + kv.first + "\" is not one of the 24 team sheet columns Turbo writes";
        if (kv.second < -1 || kv.second > kMaxPlayerId)
            return "set pieces: " + kv.first + " = " + std::to_string(kv.second) + " is outside -1.." + std::to_string(kMaxPlayerId);
    }
    return "";
}

std::vector<std::string> setpiece_diff(const SetPieceBlock& want, const SetPieceBlock& now) {
    std::vector<std::string> out;
    for (const SetPieceField& f : setpiece_fields()) {
        auto w = want.values.find(f.name);
        if (w == want.values.end()) continue;
        auto n = now.values.find(f.name);
        const int cur = n == now.values.end() ? -2 : n->second;
        if (cur != w->second)
            out.push_back(std::string(f.label) + " " + (cur == -2 ? std::string("?") : std::to_string(cur)) + " -> " + std::to_string(w->second));
    }
    return out;
}

bool setpieces_parse_current(const std::string& text, SetPieceBlock& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (text.size() > kMaxFileBytes) return fail("too large");
    ojson j = ojson::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("not a JSON object");
    auto mk = j.find("turbo_setpieces");
    auto tm = j.find("team");
    auto v = j.find("values");
    if (mk == j.end() || !mk->is_number_integer() || tm == j.end() || !tm->is_number_integer() || v == j.end() || !v->is_object())
        return fail("not a Turbo set-piece read-out");
    SetPieceBlock b;
    b.present = true;
    b.team = int(std::min<long long>(std::max<long long>(tm->get<long long>(), 0), 1000000));
    for (auto it = v->begin(); it != v->end(); ++it) {
        if (!it.value().is_number_integer()) continue;  // a column the database did not give
        const long long x = it.value().get<long long>();
        b.values[it.key()] = int(std::min<long long>(std::max<long long>(x, -1000000), 1000000));
    }
    const std::string why = setpiece_problem(b);
    if (!why.empty()) return fail(why);
    out = b;
    return true;
}

std::string setpieces_command_json(const std::string& mode, const SetPieceBlock& want, const std::string& preset) {
    ojson o = ojson::object();
    o["mode"] = mode;
    o["confirm"] = true;
    if (mode == "set") {
        o["team"] = want.team;
        o["preset"] = preset;
        ojson v = ojson::object();
        for (const SetPieceField& f : setpiece_fields()) {
            auto it = want.values.find(f.name);
            if (it != want.values.end()) v[f.name] = it->second;
        }
        o["values"] = v;
    }
    ojson c = ojson::object();
    c["op"] = "run";
    c["module"] = "tactic_setpieces";
    c["overrides"] = o;
    return c.dump();
}

std::string Preset::to_json() const {
    ojson j = ojson::object();
    j["turbo_tactic_preset"] = schema;
    j["name"] = name;
    j["note"] = note;
    j["created"] = created;
    j["game_build"] = game_build;
    if (tactic.present) j["tactic"] = block_json(tactic);
    if (settings.present) {
        ojson s = ojson::object();
        s["layout"] = settings.layout;
        s["slider_map_build"] = settings.map_build;
        ojson v = ojson::object();
        for (const auto& kv : settings.values) v[kv.first] = kv.second;
        s["values"] = v;
        if (settings.include_unverified) s["include_unverified"] = true;
        j["settings"] = s;
    }
    if (setpieces.present) {
        ojson s = ojson::object();
        s["team"] = setpieces.team;
        ojson v = ojson::object();
        for (const SetPieceField& f : setpiece_fields()) {
            auto it = setpieces.values.find(f.name);
            if (it != setpieces.values.end()) v[f.name] = it->second;
        }
        s["values"] = v;
        j["setpieces"] = s;
    }
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool Preset::parse(const std::string& text, Preset& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (text.size() > kMaxFileBytes) return fail("too large");
    ojson j = ojson::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("not a JSON object");
    auto mk = j.find("turbo_tactic_preset");
    if (mk == j.end() || !mk->is_number_integer()) return fail("not a Turbo tactic preset (no \"turbo_tactic_preset\" number)");
    Preset p;
    p.schema = int(std::min<long long>(std::max<long long>(mk->get<long long>(), 0), 1000000));
    if (p.schema != kSchema) return fail(validate(p));  // the schema message (newer / unknown), before anything else is read
    auto str = [&](const char* key, std::string& dst, bool required) {
        auto it = j.find(key);
        if (it == j.end()) return !required;
        if (!it->is_string()) return false;
        dst = it->get<std::string>();
        return true;
    };
    if (!str("name", p.name, true) || !str("note", p.note, false) || !str("created", p.created, false) || !str("game_build", p.game_build, true))
        return fail("name / note / created / game_build must be text (name and game_build are required)");
    auto t = j.find("tactic");
    if (t != j.end()) {
        std::string e;
        if (!block_parse(*t, p.tactic, &e)) return fail(e);
    }
    auto s = j.find("settings");
    if (s != j.end()) {
        if (!s->is_object()) return fail("settings is not an object");
        p.settings.present = true;
        auto l = s->find("layout");
        auto mb = s->find("slider_map_build");
        auto v = s->find("values");
        if (l == s->end() || !l->is_string() || v == s->end() || !v->is_object()) return fail("settings needs a text layout and an object values");
        p.settings.layout = l->get<std::string>();
        if (mb != s->end() && mb->is_string()) p.settings.map_build = mb->get<std::string>();
        for (auto it = v->begin(); it != v->end(); ++it) {
            if (!it.value().is_number_integer()) return fail("settings: " + it.key() + " is not a whole number");
            const long long x = it.value().get<long long>();
            if (x < -1000000 || x > 1000000) return fail("settings: " + it.key() + " is out of range");
            p.settings.values[it.key()] = int(x);
        }
        auto iu = s->find("include_unverified");
        p.settings.include_unverified = iu != s->end() && iu->is_boolean() && iu->get<bool>();
    }
    auto sp = j.find("setpieces");
    if (sp != j.end()) {
        if (!sp->is_object()) return fail("setpieces is not an object");
        p.setpieces.present = true;
        auto tm = sp->find("team");
        auto v = sp->find("values");
        if (tm == sp->end() || !tm->is_number_integer() || v == sp->end() || !v->is_object()) return fail("setpieces needs a whole-number team and an object values");
        p.setpieces.team = int(std::min<long long>(std::max<long long>(tm->get<long long>(), -1000000), 1000000));
        for (auto it = v->begin(); it != v->end(); ++it) {
            if (!it.value().is_number_integer()) return fail("setpieces: " + it.key() + " is not a whole number");
            const long long x = it.value().get<long long>();
            if (x < -1000000 || x > 1000000) return fail("setpieces: " + it.key() + " is out of range");
            p.setpieces.values[it.key()] = int(x);
        }
    }
    const std::string why = validate(p);
    if (!why.empty()) return fail(why);
    out = p;
    return true;
}

// ------------------------------------------------------------------------------------------------ the game
TacticBlock block_from_live(const tsheet::LiveTactic& lt) {
    TacticBlock b;
    b.present = true;
    b.record_name = lt.name;
    b.formation = lt.formation;
    b.source_formation = lt.source_formation;
    b.line_height = lt.line_height;
    b.build_up = lt.build_up;
    b.approach = lt.approach;
    for (int s = 0; s < kSlots; ++s) {
        const tsheet::SlotView& v = lt.slots[size_t(s)];
        SlotRec& r = b.slots[size_t(s)];
        r.position = v.position;
        r.role_group = v.group();
        r.role_id = v.role_id;
        r.focus = v.focus;
    }
    return b;
}

Preset make_preset(const std::string& name, const std::string& note, const std::string& created, const tsheet::LiveTactic* live,
                   const std::map<std::string, int>* sliders, const std::string& layout, const std::string& map_build,
                   const SetPieceBlock* setpieces, bool include_unverified) {
    Preset p;
    p.name = name;
    p.note = note;
    p.created = created;
    p.game_build = build();
    if (live) p.tactic = block_from_live(*live);
    if (sliders && !sliders->empty()) {
        p.settings.present = true;
        p.settings.layout = layout;
        p.settings.map_build = map_build;
        p.settings.values = *sliders;
        p.settings.include_unverified = include_unverified;
    }
    if (setpieces && setpieces->present && !setpieces->values.empty()) p.setpieces = *setpieces;
    return p;
}

std::string formation_note(const TacticBlock& want, const tsheet::LiveTactic& now) {
    if (want.source_formation == now.source_formation) return "";
    return "formation: preset " + (want.record_name.empty() ? std::string("?") : want.record_name) + " (source " + std::to_string(want.source_formation) +
           ") / live " + (now.name.empty() ? std::string("?") : now.name) + " (source " + std::to_string(now.source_formation) + ")";
}

std::string request_for(const TacticBlock& want, const tsheet::LiveTactic& now, tsheet::Request& out, std::vector<std::string>& notes) {
    notes.clear();
    out = tsheet::Request();
    if (!want.present) return "the preset has no tactic part";
    // The formation is never written (no writer is proven), so a preset of another formation is refused whole: no roles onto another shape
    if (want.source_formation != now.source_formation)
        return formation_note(want, now) + ": pick the preset's formation in Edit Tactic first (and leave it without 'Yes'), then Load";
    for (int s = 0; s < kSlots; ++s)
        if (want.slots[size_t(s)].position != now.slots[size_t(s)].position)
            return "the slots hold other positions than the preset's (slot " + std::to_string(s) + ": preset " + std::to_string(want.slots[size_t(s)].position) +
                   ", live " + std::to_string(now.slots[size_t(s)].position) + "): pick the preset's formation in Edit Tactic first, then Load";
    // Line Height and Defensive Approach go together: Turbo writes them band-consistent (the approach is the line's band, team_sheet.h make_plan
    // refuses any other pair). 3.0.1 (GUI30H): the Line Height wins and the approach follows its band (b153e48 dropped BOTH when the preset's
    // approach was of another band, e.g. GUI30H_B line 60 / Deep, and nothing said so). Only a line in Aggressive's band (91..100, the approach
    // Turbo does not write: team_sheet.cpp approach table) or outside 1..100 stays as it is, with a note.
    if (want.line_height < 1 || want.line_height > 100) {
        notes.push_back("Line Height " + std::to_string(want.line_height) + " is outside 1..100: Line Height and Defensive Approach stay as they are");
    } else if (want.line_height > tsheet::max_writable_line()) {
        notes.push_back("Line Height " + std::to_string(want.line_height) + " lies in the band of Defensive Approach " +
                        approach_text(tsheet::line_band(want.line_height)) + ", which Turbo does not write (Line Height 1.." +
                        std::to_string(tsheet::max_writable_line()) + " only): Line Height and Defensive Approach stay as they are");
    } else {
        const int band = tsheet::line_band(want.line_height);
        out.line_height = want.line_height;
        out.approach = band;
        if (want.approach != band)
            notes.push_back("Defensive Approach " + approach_text(want.approach) + " -> " + approach_text(band) + " to match Line Height " +
                            std::to_string(want.line_height) + " (Turbo writes the two band-consistent)");
    }
    if (tsheet::build_up_writable(want.build_up)) out.build_up = want.build_up;
    else notes.push_back("Build-Up Style " + build_up_text(want.build_up) + " is not written (not decoded yet): it stays as it is");
    for (int s = 0; s < kSlots; ++s) {
        const SlotRec& w = want.slots[size_t(s)];
        if (w.role_group != tsheet::role_group(now.slots[size_t(s)].role)) {
            notes.push_back("slot " + std::to_string(s) + ": the role group differs (preset " + std::to_string(w.role_group) + ", live " +
                            std::to_string(tsheet::role_group(now.slots[size_t(s)].role)) + "): left alone");
            continue;
        }
        out.slots.push_back({s, w.role_id, w.focus});
    }
    return "";
}

static std::vector<std::string> diff_impl(const TacticBlock& want, const tsheet::LiveTactic& now, bool unwritten) {
    std::vector<std::string> v;
    tsheet::Request r;
    std::vector<std::string> notes;
    if (!request_for(want, now, r, notes).empty()) return v;  // another formation: nothing is comparable
    const char* nw = ": not written";
    if (r.approach >= 0 && r.approach != now.approach) v.push_back("Defensive Approach " + approach_text(now.approach) + " -> " + approach_text(r.approach));
    else if (unwritten && r.approach < 0 && want.approach != now.approach)
        v.push_back("Defensive Approach " + approach_text(now.approach) + " -> " + approach_text(want.approach) + nw);
    if (r.line_height >= 0 && r.line_height != now.line_height) v.push_back("Line Height " + std::to_string(now.line_height) + " -> " + std::to_string(r.line_height));
    else if (unwritten && r.line_height < 0 && want.line_height != now.line_height)
        v.push_back("Line Height " + std::to_string(now.line_height) + " -> " + std::to_string(want.line_height) + nw);
    if (r.build_up >= 0 && r.build_up != now.build_up) v.push_back("Build-Up Style " + build_up_text(now.build_up) + " -> " + build_up_text(r.build_up));
    else if (unwritten && r.build_up < 0 && want.build_up != now.build_up)
        v.push_back("Build-Up Style " + build_up_text(now.build_up) + " -> " + build_up_text(want.build_up) + nw);
    std::vector<bool> sent(size_t(kSlots), false);
    for (const tsheet::SlotChange& c : r.slots) {
        if (c.slot >= 0 && c.slot < kSlots) sent[size_t(c.slot)] = true;
        const tsheet::SlotView& l = now.slots[size_t(c.slot)];
        if (c.role_id != l.role_id || c.focus != l.focus)
            v.push_back("slot " + std::to_string(c.slot) + " role " + std::to_string(l.role_id) + " / focus " + std::to_string(l.focus) + " -> role " +
                        std::to_string(c.role_id) + " / focus " + std::to_string(c.focus));
    }
    if (unwritten)
        for (int s = 0; s < kSlots; ++s) {
            const SlotRec& w = want.slots[size_t(s)];
            const tsheet::SlotView& l = now.slots[size_t(s)];
            if (!sent[size_t(s)] && (w.role_id != l.role_id || w.focus != l.focus))
                v.push_back("slot " + std::to_string(s) + " role " + std::to_string(l.role_id) + " / focus " + std::to_string(l.focus) + " -> role " +
                            std::to_string(w.role_id) + " / focus " + std::to_string(w.focus) + nw);
        }
    return v;
}

std::vector<std::string> diff_lines(const TacticBlock& want, const tsheet::LiveTactic& now) { return diff_impl(want, now, true); }

std::vector<std::string> written_diff_lines(const TacticBlock& want, const tsheet::LiveTactic& now) { return diff_impl(want, now, false); }

std::vector<std::string> unwritten_lines(const std::vector<std::string>& diff) {
    std::vector<std::string> v;
    const std::string nw = ": not written";
    for (const std::string& d : diff)
        if (d.size() >= nw.size() && d.compare(d.size() - nw.size(), nw.size(), nw) == 0) v.push_back(d);
    return v;
}

std::string summary(const Preset& p) {
    std::string s;
    if (p.tactic.present) {
        s += (p.tactic.record_name.empty() ? std::string("tactic") : p.tactic.record_name) + ", Defensive Approach " + approach_text(p.tactic.approach) +
             ", Line Height " + std::to_string(p.tactic.line_height) + ", Build-Up Style " + build_up_text(p.tactic.build_up) + "; " + std::to_string(kSlots) +
             " roles";
    }
    if (p.settings.present) s += std::string(s.empty() ? "" : "; ") + std::to_string(p.settings.values.size()) + " sliders";
    if (p.setpieces.present) s += std::string(s.empty() ? "" : "; ") + std::to_string(p.setpieces.values.size()) + " set-piece fields";
    return s;
}

std::string live_why_plain(const std::string& why) {
    if (why.empty()) return "";
    auto has = [&](const char* n) { return why.find(n) != std::string::npos; };
    if (has("active tactic record"))  // seen in every hub read of Cassio's Ajax and Sparta careers (10-18, 10-19): not a hub problem
        return "Turbo cannot read this career's tactic: its team sheet is not laid out the way Turbo knows. Tactic presets cannot be applied in "
               "this career yet; slider presets still work.";
    if (has("layout mismatch"))
        return "Turbo cannot read your tactic right now: the team sheet is not laid out the way Turbo knows. Close Team Management and press "
               "Refresh; if this stays, Turbo cannot read this career's tactic yet.";
    if (has("not in a career")) return "Load your career and wait in its main hub, so Turbo can read your tactic.";
    if (has("locating") || has("checking the service")) return "Turbo is still looking for your team sheet. Wait a few seconds, then press Refresh.";
    if (has("your club") || has("the game's club")) return "Turbo does not know your club yet. Go to your career's main hub and wait a few seconds.";
    return "Go to your career's main hub and close Team Management.";
}

std::string scan_line(size_t listed, const std::vector<std::string>& errors, const std::string& dir) {
    std::string s = "store scan: " + std::to_string(listed) + " listed, " + std::to_string(errors.size()) + " skipped in " + dir;
    for (size_t i = 0; i < errors.size(); ++i) {
        std::string e = errors[i];
        if (e.size() > 160) e = e.substr(0, 157) + "...";
        s += (i == 0 ? "; skipped: " : " | ") + e;
    }
    return s;
}

// ------------------------------------------------------------------------------------------------ files
fs::path presets_dir(const fs::path& out_dir) { return out_dir / dir_name(); }
fs::path last_load_path(const fs::path& out_dir) { return out_dir / last_load_file(); }
std::string file_name(const Preset& p) { return gsettings::safe_name(p.name) + ".json"; }

std::vector<Preset> load_presets(const fs::path& dir, std::vector<std::string>* errors) {
    std::vector<Preset> v;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return v;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path f = it->path();
        const std::string n = f.filename().u8string();
        if (n.empty() || n[0] == '.' || lower(f.extension().u8string()) != ".json") continue;
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string text, why;
        Preset p;
        if (!read_small_file(f, text)) why = "cannot be read (or larger than " + std::to_string(kMaxFileBytes / 1024) + " KB)";
        else if (!Preset::parse(text, p, &why)) {
        } else {
            p.file = f;
            v.push_back(p);
            continue;
        }
        if (errors) errors->push_back(n + ": " + why);
    }
    std::sort(v.begin(), v.end(), [](const Preset& a, const Preset& b) { return lower(a.name) < lower(b.name); });
    return v;
}

std::string unique_name(const std::vector<Preset>& have, const std::string& wanted) {
    auto taken = [&](const std::string& n) {
        for (const Preset& q : have)
            if (gsettings::same_name(q.name, n) || gsettings::safe_name(q.name) == gsettings::safe_name(n)) return true;
        return false;
    };
    std::string base = wanted;
    while (!base.empty() && base.back() == ' ') base.pop_back();
    while (!base.empty() && base.front() == ' ') base.erase(base.begin());
    if (base.size() > kMaxNameLen - 6) base.resize(kMaxNameLen - 6);
    if (base.empty()) base = "Preset";
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; ++i)
        if (!taken(base + " " + std::to_string(i))) return base + " " + std::to_string(i);
    return base + " copy";
}

bool write_preset(const fs::path& dir, Preset& p, std::string* err) {
    const std::string why = validate(p);
    if (!why.empty()) {
        if (err) *err = "the preset is not valid: " + why;
        return false;
    }
    const fs::path f = dir / file_name(p);
    if (!gsettings::write_text(f, p.to_json(), err)) return false;
    p.file = f;
    return true;
}

bool delete_preset(const Preset& p, std::string* err) {
    std::error_code ec;
    if (p.file.empty()) {
        if (err) *err = "the preset has no file";
        return false;
    }
    fs::remove(p.file, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ the latest load
std::string LastLoad::to_json() const {
    ojson j = ojson::object();
    j["turbo_tactic_last_load"] = schema;
    j["preset"] = preset;
    j["when"] = when;
    j["state"] = state;
    j["note"] = note;
    j["team"] = team;
    j["session"] = session;
    j["load_gen"] = load_gen;
    if (before.present) j["before"] = block_json(before);
    if (after.present) j["after"] = block_json(after);
    ojson s = ojson::object();
    s["staged"] = settings_staged;
    s["preset"] = settings_preset;
    s["skipped"] = settings_skipped;
    if (!settings_previous.empty()) s["previous"] = settings_previous;
    j["settings"] = s;
    j["setpieces_sent"] = setpieces_sent;
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool LastLoad::parse(const std::string& text, LastLoad& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (text.size() > kMaxFileBytes) return fail("too large");
    ojson j = ojson::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("not a JSON object");
    auto mk = j.find("turbo_tactic_last_load");
    if (mk == j.end() || !mk->is_number_integer() || mk->get<long long>() != 1) return fail("not a Turbo last-load file of schema 1");
    LastLoad l;
    auto str = [&](const char* key, std::string& dst) {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) dst = it->get<std::string>();
    };
    str("preset", l.preset);
    str("when", l.when);
    str("state", l.state);
    str("note", l.note);
    str("session", l.session);
    auto t = j.find("team");
    if (t != j.end() && t->is_number_integer()) l.team = int(t->get<long long>());
    auto g = j.find("load_gen");
    if (g != j.end() && g->is_number_integer()) l.load_gen = g->get<long long>();
    for (const char* key : {"before", "after"}) {
        auto b = j.find(key);
        if (b == j.end()) continue;
        TacticBlock tb;
        std::string e;
        if (!block_parse(*b, tb, &e)) return fail(std::string(key) + ": " + e);
        if (!tactic_problem(tb).empty()) return fail(std::string(key) + ": " + tactic_problem(tb));
        (std::string(key) == "before" ? l.before : l.after) = tb;
    }
    auto s = j.find("settings");
    if (s != j.end() && s->is_object()) {
        auto st = s->find("staged");
        if (st != s->end() && st->is_boolean()) l.settings_staged = st->get<bool>();
        auto sp = s->find("preset");
        if (sp != s->end() && sp->is_string()) l.settings_preset = sp->get<std::string>();
        auto sk = s->find("skipped");
        if (sk != s->end() && sk->is_array())
            for (const auto& x : *sk)
                if (x.is_string()) l.settings_skipped.push_back(x.get<std::string>());
        auto pv = s->find("previous");
        if (pv != s->end() && pv->is_string()) l.settings_previous = pv->get<std::string>();
    }
    auto ss = j.find("setpieces_sent");
    if (ss != j.end() && ss->is_boolean()) l.setpieces_sent = ss->get<bool>();
    if (l.preset.empty()) return fail("no preset name");
    out = l;
    return true;
}

bool load_last(const fs::path& p, LastLoad& out, std::string* err) {
    std::string text;
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    if (!read_small_file(p, text)) {
        if (err) *err = "cannot be read";
        return false;
    }
    return LastLoad::parse(text, out, err);
}

bool save_last(const fs::path& p, const LastLoad& l, std::string* err) { return gsettings::write_text(p, l.to_json(), err); }

std::string undo_request(const LastLoad& l, const tsheet::LiveTactic& now, tsheet::Request& out, std::vector<std::string>& notes) {
    if (!l.before.present) return "the last load kept no tactic values to put back";
    return request_for(l.before, now, out, notes);
}

std::string reapply_prompt(const LastLoad& l, const tsheet::LiveTactic& now, const std::string& session, long long load_gen) {
    if (l.state != "loaded" || !l.after.present || l.team != now.at.user) return "";
    const std::vector<std::string> d = written_diff_lines(l.after, now);  // 3.0.1: a value the load did not write is never "not held now"
    if (d.empty()) return "";
    const bool earlier = l.session != session || l.load_gen != load_gen;
    std::string s = std::string("Preset \"") + l.preset + "\" was loaded " + (earlier ? "in an earlier career load" : "in this career load") +
                    " and your live tactic does not hold it now (" + d.front() + (d.size() > 1 ? ", ..." : "") + "). Re-apply it?";
    return s;
}

}  // namespace tpreset
}  // namespace turbo
