// FC 27 LE Turbo GUI - the store of edits Turbo writes again at every career load (see reapply.h)
#include "reapply.h"

#include <fstream>
#include <sstream>

#include "callnames.h"
#include "nlohmann/json.hpp"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

const std::vector<std::string>& kit_colour_prefixes() {
    // ui_identity.cpp team_colours_editor: the kit colour pickers
    static const std::vector<std::string> p = {"teamcolorprim",         "teamcolorsec",          "teamcolortert",
                                               "jerseynamecolor",       "jerseynameoutlinecolor", "jerseynumbercolorprim",
                                               "jerseynumbercolorsec",  "jerseynumbercolorter",  "shortsnumbercolorprim",
                                               "shortsnumbercolorsec",  "shortsnumbercolorter"};
    return p;
}

bool kit_colour_field(const std::string& field) {
    if (field.size() < 2) return false;
    const char ch = field.back();
    if (ch != 'r' && ch != 'g' && ch != 'b') return false;
    const std::string prefix = field.substr(0, field.size() - 1);
    for (const auto& p : kit_colour_prefixes())
        if (p == prefix) return true;
    return false;
}

void ReapplyStore::set_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& team, const std::string& when,
                                 const std::string& field, int64_t value) {
    if (teamkitid < 0) teamkitid = -1;
    KitEdit& k = kits[KitKey{teamtechid, kittype, teamkitid}];
    k.teamtechid = teamtechid;
    k.kittype = kittype;
    k.teamkitid = teamkitid;
    if (!team.empty()) k.team = team;
    if (!when.empty()) k.when = when;
    k.fields[field] = value;
}

bool ReapplyStore::forget_kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) {
    return kits.erase(KitKey{teamtechid, kittype, teamkitid < 0 ? -1 : teamkitid}) > 0;
}

const KitEdit* ReapplyStore::kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) const {
    auto it = kits.find(KitKey{teamtechid, kittype, teamkitid < 0 ? -1 : teamkitid});
    if (it == kits.end() && teamkitid >= 0) it = kits.find(KitKey{teamtechid, kittype, -1});
    return it == kits.end() ? nullptr : &it->second;
}

size_t ReapplyStore::kits_of_type(int64_t teamtechid, int64_t kittype) const {
    size_t n = 0;
    for (const auto& kv : kits) n += std::get<0>(kv.first) == teamtechid && std::get<1>(kv.first) == kittype;
    return n;
}

void ReapplyStore::set_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, const std::string& when) {
    CallnameEdit& c = callnames[playerid];
    c.playerid = playerid;
    c.commentaryid = commentaryid;
    c.player = player;
    c.from = from;
    c.when = when;
}

bool ReapplyStore::forget_callname(int64_t playerid) { return callnames.erase(playerid) > 0; }

const CallnameEdit* ReapplyStore::callname(int64_t playerid) const {
    auto it = callnames.find(playerid);
    return it == callnames.end() ? nullptr : &it->second;
}

std::string reapply_json(const ReapplyStore& s) {
    json j = json::object();
    j["turbo_reapply"] = 1;
    j["note"] = "Edits FC 27 forgets when a career loads (it reloads teamkits and playernamemap from its base data). Turbo writes "
                "them again when it connects to a newly loaded career. Forget an entry in Teams > Colours or Players > Callname.";
    json kits = json::array();
    for (const auto& kv : s.kits) {
        const KitEdit& k = kv.second;
        json fields = json::object();
        for (const auto& f : k.fields) fields[f.first] = f.second;
        kits.push_back({{"teamtechid", k.teamtechid}, {"teamkittypetechid", k.kittype}, {"teamkitid", k.teamkitid},
                        {"team", k.team}, {"when", k.when}, {"fields", fields}});
    }
    j["kits"] = kits;
    json names = json::array();
    for (const auto& kv : s.callnames) {
        const CallnameEdit& c = kv.second;
        names.push_back({{"playerid", c.playerid}, {"commentaryid", c.commentaryid}, {"player", c.player}, {"from", c.from}, {"when", c.when}});
    }
    j["playernamemap"] = names;
    // error_handler::replace: a name with broken UTF-8 (a player name read from memory) must not throw
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

static bool int_of(const json& o, const char* key, int64_t& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_number_integer()) return false;
    out = it->get<int64_t>();
    return true;
}

static std::string str_of(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool parse_reapply_json(const std::string& text, ReapplyStore& out, std::string* err, size_t* dropped) {
    out = ReapplyStore{};
    if (dropped) *dropped = 0;
    size_t bad = 0;
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        int64_t ver = 0;
        if (!int_of(j, "turbo_reapply", ver) || ver != 1) {
            if (err) *err = "not a Turbo re-apply store (no \"turbo_reapply\": 1)";
            return false;
        }
        if (auto it = j.find("kits"); it != j.end() && it->is_array()) {
            for (const json& e : *it) {
                int64_t tid = 0, type = 0, kitid = -1;
                if (!e.is_object() || !int_of(e, "teamtechid", tid) || !int_of(e, "teamkittypetechid", type) || tid <= 0 || type < 0) {
                    ++bad;
                    continue;
                }
                if (!int_of(e, "teamkitid", kitid)) kitid = -1;
                auto fi = e.find("fields");
                if (fi == e.end() || !fi->is_object() || fi->empty()) {
                    ++bad;
                    continue;
                }
                for (auto f = fi->begin(); f != fi->end(); ++f) {
                    // only the colour channels Teams > Colours writes: a hand-edited key column (teamtechid,
                    // teamkitid, ...) or any other int is never written back at a career load
                    if (!kit_colour_field(f.key()) || !f.value().is_number_integer()) {
                        ++bad;
                        continue;
                    }
                    out.set_kit_field(tid, type, kitid, str_of(e, "team"), str_of(e, "when"), f.key(), f.value().get<int64_t>());
                }
            }
        }
        if (auto it = j.find("playernamemap"); it != j.end() && it->is_array()) {
            for (const json& e : *it) {
                int64_t pid = 0, cid = 0;
                // the ids every commentary bank uses for names; 900000 is "no callname" and never a kept edit
                if (!e.is_object() || !int_of(e, "playerid", pid) || !int_of(e, "commentaryid", cid) || pid <= 0 || cid <= kNoCallname ||
                    cid > kCallnameMax) {
                    ++bad;
                    continue;
                }
                out.set_callname(pid, cid, str_of(e, "player"), str_of(e, "from"), str_of(e, "when"));
            }
        }
    } catch (const std::exception& e) {
        out = ReapplyStore{};
        if (err) *err = std::string("cannot read the store: ") + e.what();
        return false;
    }
    if (dropped) *dropped = bad;
    return true;
}

fs::path reapply_store_path(const fs::path& le_root) { return le_root / "turbo_output" / "reapply_edits.json"; }

fs::path reapply_unreadable_path(const fs::path& p) { return p.parent_path() / "reapply_edits.unreadable.json"; }

bool load_reapply_store(const fs::path& p, ReapplyStore& out, std::string* err, size_t* dropped) {
    out = ReapplyStore{};
    if (dropped) *dropped = 0;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + p.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string why;
    if (!parse_reapply_json(ss.str(), out, &why, dropped)) {
        if (err) *err = p.filename().string() + ": " + why;
        return false;
    }
    return true;
}

bool save_reapply_store(const fs::path& p, const ReapplyStore& s, std::string* err, bool set_aside) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (set_aside && fs::exists(p, ec)) {
        const fs::path keep = reapply_unreadable_path(p);
        fs::remove(keep, ec);
        ec.clear();
        fs::rename(p, keep, ec);
        if (ec) {
            if (err) *err = "cannot set the unreadable " + p.filename().string() + " aside: " + ec.message();
            return false;
        }
    }
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << reapply_json(s);
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        // Windows: a rename over a file another program holds open can fail; replace in two steps
        ec.clear();
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.string();
            return false;
        }
    }
    return true;
}

}  // namespace turbo
