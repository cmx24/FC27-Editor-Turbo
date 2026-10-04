// FC 27 LE Turbo GUI - player callnames for the loaded commentary language (see callnames.h and docs/callnames.md)
#include "callnames.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace turbo {

namespace fs = std::filesystem;

static const char* kPackPrefix = "commentaryfull_";

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "ita_it" from "commentaryfull_ita_it" / "commentaryfull_ita_it.toc"; "" when the name is not a language pack
static std::string pack_code(const std::string& file_name, bool toc) {
    std::string n = lower(file_name);
    if (n.rfind(kPackPrefix, 0) != 0) return "";
    std::string code = n.substr(std::string(kPackPrefix).size());
    if (toc) {
        if (code.size() < 5 || code.substr(code.size() - 4) != ".toc") return "";
        code = code.substr(0, code.size() - 4);
    }
    // xxx_yy: three letters, underscore, two letters
    if (code.size() != 6 || code[3] != '_') return "";
    for (size_t i = 0; i < code.size(); ++i)
        if (i != 3 && !std::isalpha(static_cast<unsigned char>(code[i]))) return "";
    return code;
}

std::vector<CommentaryPack> installed_commentary_packs(const fs::path& game_root) {
    std::vector<CommentaryPack> out;
    if (game_root.empty()) return out;
    auto add = [&](const std::string& code, bool downloaded) {
        for (auto& p : out)
            if (p.code == code) {
                p.downloaded = p.downloaded || downloaded;
                return;
            }
        out.push_back({code, downloaded});
    };
    std::error_code ec;
    // languages the user added: <game>\commentary\commentaryfull_<lang>\ (folder with cas files) + .toc beside it
    for (const auto& e : fs::directory_iterator(game_root / "commentary", ec)) {
        std::string code = pack_code(e.path().filename().string(), !e.is_directory(ec));
        if (!code.empty()) add(code, true);
    }
    // the base game data: <game>\Data\Win32\commentaryfull_<lang>.toc
    for (const auto& e : fs::directory_iterator(game_root / "Data" / "Win32", ec)) {
        std::string code = pack_code(e.path().filename().string(), !e.is_directory(ec));
        if (!code.empty()) add(code, false);
    }
    std::sort(out.begin(), out.end(), [](const CommentaryPack& a, const CommentaryPack& b) { return a.code < b.code; });
    return out;
}

std::string pick_commentary_language(const std::vector<CommentaryPack>& packs, const std::string& chosen, std::string* why) {
    std::string note;
    auto set_why = [&](const std::string& s) { if (why) *why = note + s; };
    if (packs.empty()) {
        set_why("no commentary language pack found in the game folder");
        return "";
    }
    std::string want = lower(chosen);
    if (!want.empty()) {
        for (const auto& p : packs)
            if (p.code == want) {
                set_why("chosen in Turbo's settings");
                return p.code;
            }
        note = "'" + chosen + "' is not installed; detected instead: ";
    }
    std::vector<const CommentaryPack*> downloaded;
    for (const auto& p : packs)
        if (p.downloaded) downloaded.push_back(&p);
    if (downloaded.size() == 1) {
        set_why("the one commentary language downloaded into <game>\\commentary (the game downloads the language picked "
                "in its audio settings)");
        return downloaded[0]->code;
    }
    if (downloaded.size() > 1) {
        set_why("several downloaded languages: pick the one the game uses");
        return downloaded[0]->code;
    }
    for (const auto& p : packs)
        if (p.code == "eng_us") {
            set_why("no downloaded language: the base game's English commentary");
            return p.code;
        }
    set_why("the only language pack found");
    return packs[0].code;
}

bool parse_spoken_list(const std::string& text, std::unordered_set<int64_t>& out, std::string* lang, std::string* err) {
    out.clear();
    if (lang) lang->clear();
    long long expected = -1;
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first && line.rfind("#turbo-spoken ", 0) == 0) {
            std::istringstream h(line.substr(14));
            std::string l;
            h >> l >> expected;
            if (lang) *lang = l;
            first = false;
            continue;
        }
        first = false;
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        char* stop = nullptr;
        long long id = std::strtoll(line.c_str() + start, &stop, 10);
        if (!stop || stop == line.c_str() + start) continue;
        if (*stop != '\0' && *stop != '\t' && *stop != ' ' && *stop != '#' && *stop != ',' && *stop != ';') continue;
        if (id < kCallnameMin || id > kCallnameMax) continue;
        out.insert(static_cast<int64_t>(id));
    }
    if (out.empty()) {
        if (err) *err = "no commentary ids (900000..965000) in the list";
        return false;
    }
    if (expected >= 0 && static_cast<long long>(out.size()) != expected) {
        if (err) *err = "the header announces " + std::to_string(expected) + " ids but the file holds " + std::to_string(out.size());
        return false;
    }
    return true;
}

fs::path spoken_list_path(const fs::path& le_root, const std::string& lang) {
    return le_root / "turbo" / "callnames" / ("spoken_" + lang + ".txt");
}

fs::path master_list_path(const fs::path& le_root, const std::string& lang) {
    return le_root / "turbo" / "callnames" / "masters" / (lang + ".json");
}

bool parse_master_list_json(const std::string& text, const std::string& lang, MasterList& out, std::string* err) {
    using nlohmann::json;
    out = MasterList{};
    auto fail = [&](const std::string& why) {
        out = MasterList{};
        if (err) *err = why;
        return false;
    };
    // The file is written by the user's tool and may be edited by hand: every access is type-checked, and the parse
    // runs without exceptions (a json type error must never reach the GUI's frame)
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return fail("not JSON (make it again with turbo\\tools\\import_callname_masters.py)");
        auto str = [&](const char* key) {
            auto it = j.find(key);
            return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
        };
        out.lang = str("language");
        out.source = str("source");
        out.built = str("built");
        // "fc27": an FC 27 master; anything else (or none: the 1.0.2 tool's first lists) is the user's FC 26 list
        out.game = lower(str("game")) == "fc27" ? "fc27" : "fc26";
        if (!lang.empty() && !out.lang.empty() && lower(out.lang) != lower(lang))
            return fail("a list for '" + out.lang + "', not '" + lang + "'");
        // ids are positive integers; a whole-number float (a spreadsheet's 216435.0) is accepted, anything else skipped
        auto id_of = [](const json& v, int64_t& id) {
            if (v.is_number_integer()) {
                id = v.get<int64_t>();
            } else if (v.is_number_float()) {
                const double d = v.get<double>();
                // range first: converting a double outside int64 is undefined
                if (!(d > 0.0 && d < 9.0e15) || d != static_cast<double>(static_cast<int64_t>(d))) return false;
                id = static_cast<int64_t>(d);
            } else {
                return false;
            }
            return id > 0;
        };
        int64_t id = 0;
        if (auto it = j.find("real_players"); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (id_of(v, id)) out.real_players.insert(id);
        if (auto it = j.find("generic_ids"); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (id_of(v, id)) out.generic_ids.insert(id);
        if (auto it = j.find("names"); it != j.end() && it->is_object())
            for (auto n = it->begin(); n != it->end(); ++n) {
                if (!n.value().is_string()) continue;
                char* stop = nullptr;
                long long pid = std::strtoll(n.key().c_str(), &stop, 10);
                if (stop && *stop == '\0' && pid > 0) out.names[static_cast<int64_t>(pid)] = n.value().get<std::string>();
            }
        if (out.real_players.empty() && out.generic_ids.empty()) return fail("the list holds no player or commentary ids");
        out.lang = out.lang.empty() ? lang : lower(out.lang);
        return true;
    } catch (const std::exception& e) {
        return fail(std::string("unreadable: ") + e.what());
    }
}

std::string own_recording_source_name(int own, SpokenSet::From game_from, const std::string& masters) {
    const char* game = game_from == SpokenSet::From::BankCapture ? "the bank capture" : "the game's audio service";
    if ((own & kOwnFromGame) && (own & kOwnFromMasters)) return std::string(game) + " and " + masters;
    if (own & kOwnFromGame) return game;
    if (own & kOwnFromMasters) return masters;
    return "";
}

const char* callname_source_name(CallnameSource s) {
    switch (s) {
        case CallnameSource::PlayerSpecific: return "player-specific (playernamemap)";
        case CallnameSource::CommonName: return "common name";
        case CallnameSource::LastName: return "last name";
        default: return "none";
    }
}

CallnameInfo resolve_callname(int64_t playerid, int64_t commonnameid, int64_t lastnameid,
                              const std::unordered_map<int64_t, int64_t>& playernamemap,
                              const std::unordered_map<int64_t, int64_t>& name_commentary) {
    CallnameInfo info;
    auto pm = playernamemap.find(playerid);
    if (pm != playernamemap.end() && pm->second > kNoCallname) {
        info.commentaryid = pm->second;
        info.source = CallnameSource::PlayerSpecific;
        return info;
    }
    if (commonnameid > 0) {
        // FC 27's match code (docs/re/inmatch-callnames.md): a player with a common name is called by that name's
        // commentary id even when it has none; it never falls back to the last name
        auto it = name_commentary.find(commonnameid);
        info.source = CallnameSource::CommonName;
        info.nameid = commonnameid;
        if (it != name_commentary.end() && it->second > kNoCallname) info.commentaryid = it->second;
        return info;
    }
    if (lastnameid > 0) {
        auto it = name_commentary.find(lastnameid);
        if (it != name_commentary.end() && it->second > kNoCallname) {
            info.commentaryid = it->second;
            info.source = CallnameSource::LastName;
            info.nameid = lastnameid;
            return info;
        }
    }
    return info;
}

bool callname_filter_match(const std::string& text, const std::string& name, int64_t id) {
    if (text.empty()) return true;
    std::string t = lower(text);
    size_t start = t.find_first_not_of(" \t");
    if (start == std::string::npos) return true;
    t = t.substr(start);
    bool digits = !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    if (digits && std::to_string(id).rfind(t, 0) == 0) return true;
    return lower(name).find(t) != std::string::npos;
}

void CallnameIndex::clear() {
    playernamemap.clear();
    playernamemap_rec.clear();
    name_commentary.clear();
    name_users.clear();
    used_ids.clear();
    names.clear();
    players.clear();
    model_version = 0;
    built = false;
}

static bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void Callnames::refresh(const fs::path& le_root, const fs::path& game_root, const std::string& chosen) {
    no_game_root = game_root.empty();
    packs = installed_commentary_packs(game_root);
    lang = pick_commentary_language(packs, chosen, &lang_why);
    spoken = SpokenSet{};
    list_error.clear();
    list_path.clear();
    cache_error.clear();
    cache_path.clear();
    masters = MasterList{};
    masters_path.clear();
    masters_error.clear();
    spoken.lang = lang;
    if (!lang.empty()) {
        // 0. the user's FC 26 list: players with their own recording, independent of the spoken surnames below
        fs::path m = master_list_path(le_root, lang);
        masters_path = m.string();
        std::error_code mec;
        if (fs::exists(m, mec)) {
            std::string mtext, merr;
            if (!read_text(m, mtext)) {
                masters_error = "cannot read " + m.string();
            } else if (!parse_master_list_json(mtext, lang, masters, &merr)) {
                masters_error = m.filename().string() + ": " + merr;
            } else {
                masters.file = m.string();
            }
        }
        // 1. the hand-made list (an override)
        fs::path p = spoken_list_path(le_root, lang);
        list_path = p.string();
        std::string text;
        std::error_code ec;
        if (fs::exists(p, ec)) {
            std::string file_lang, err;
            if (!read_text(p, text)) {
                list_error = "cannot read " + p.string();
            } else if (!parse_spoken_list(text, spoken.ids, &file_lang, &err)) {
                list_error = p.filename().string() + ": " + err;
                spoken.ids.clear();
            } else if (!file_lang.empty() && lower(file_lang) != lang) {
                list_error = p.filename().string() + " is a list for '" + file_lang + "', not '" + lang + "'";
                spoken.ids.clear();
            } else {
                spoken.verified = true;
                spoken.from = SpokenSet::From::ListFile;
                spoken.source = p.filename().string() + " (" + std::to_string(spoken.ids.size()) + " spoken ids, hand-made list)";
            }
        }
        // 2. Turbo's capture of the loaded bank (cached)
        fs::path c = spoken_cache_path(le_root, lang);
        cache_path = c.string();
        if (fs::exists(c, ec)) {
            BankCache cache;
            std::string err;
            if (!read_text(c, text)) {
                cache_error = "cannot read " + c.string();
            } else if (!parse_bank_cache_json(text, cache, &err)) {
                cache_error = c.filename().string() + ": " + err;
            } else if (!cache.lang.empty() && lower(cache.lang) != lang) {
                cache_error = c.filename().string() + " is a capture for '" + cache.lang + "', not '" + lang + "'";
            } else if (spoken.verified) {
                // the hand-made list wins for the surnames; the players with recordings still come from the cache
                spoken.players = cache.players;
                spoken.players_from = cache.kind == "game audio service" ? SpokenSet::From::GameAudio : SpokenSet::From::BankCapture;
                spoken.players_checked = cache.checked_players;
                spoken.source += cache.kind == "game audio service" ? "; players with recordings from the game's audio service"
                                                                   : "; players with recordings from the bank capture";
            } else {
                spoken.verified = true;
                spoken.from = cache.kind == "game audio service" ? SpokenSet::From::GameAudio : SpokenSet::From::BankCapture;
                spoken.ids = cache.surnames;
                spoken.players = cache.players;
                spoken.players_from = spoken.from;
                spoken.players_checked = cache.checked_players;
                spoken.source = cache.source;
            }
        }
    }
    if (!spoken.verified) {
        spoken.from = SpokenSet::From::Fallback;
        spoken.source = "no spoken-id list or bank capture for this language: every commentary id playernames uses counts as spoken (unverified)";
        spoken.ids = index.used_ids;
    }
    refreshed = true;
}

bool Callnames::apply_capture(const BankCapture& c, const fs::path& le_root, const std::string& when, const std::string& build,
                              std::string* err) {
    if (lang.empty()) {
        if (err) *err = "no commentary language detected";
        return false;
    }
    if (!c.ok) {
        if (err) *err = c.note.empty() ? "the capture found nothing" : c.note;
        return false;
    }
    fs::path p = spoken_cache_path(le_root, lang);
    cache_path = p.string();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + p.string();
            return false;
        }
        f << bank_cache_json(c, lang, when, build);
    }
    cache_error.clear();
    BankCache cache;
    std::string perr;
    std::string text;
    if (!read_text(p, text) || !parse_bank_cache_json(text, cache, &perr)) {
        if (err) *err = "the cache just written cannot be read back: " + perr;
        return false;
    }
    if (spoken.from == SpokenSet::From::ListFile) {
        spoken.players = cache.players;  // the hand-made list keeps the surnames
    } else {
        spoken.verified = true;
        spoken.from = cache.kind == "game audio service" ? SpokenSet::From::GameAudio : SpokenSet::From::BankCapture;
        spoken.ids = cache.surnames;
        spoken.players = cache.players;
        spoken.source = cache.source;
    }
    spoken.players_from = cache.kind == "game audio service" ? SpokenSet::From::GameAudio : SpokenSet::From::BankCapture;
    spoken.players_checked = cache.checked_players;
    return true;
}

void Callnames::build_index(Database& db, const Model& model, const std::unordered_map<int64_t, std::string>& names) {
    index.clear();
    index.model_version = model.version();
    if (const Table* t = db.table("playernames"); t && t->has("nameid") && t->has("commentaryid")) {
        Snapshot s;
        if (s.load(db.memory(), *t)) {
            const Field& fid = *t->field("nameid");
            const Field& fc = *t->field("commentaryid");
            for (uint32_t i : s.valid) {
                int64_t cid = s.get_int(i, fc);
                index.name_commentary[s.get_int(i, fid)] = cid;
                if (cid > kNoCallname) index.used_ids.insert(cid);
            }
        }
    }
    if (const Table* t = db.table("playernamemap"); t && t->has("playerid") && t->has("commentaryid")) {
        Snapshot s;
        if (s.load(db.memory(), *t)) {
            const Field& fp = *t->field("playerid");
            const Field& fc = *t->field("commentaryid");
            for (uint32_t i : s.valid) {
                int64_t pid = s.get_int(i, fp);
                index.playernamemap[pid] = s.get_int(i, fc);
                index.playernamemap_rec[pid] = s.addr(i);
            }
        }
    }
    if (const Table* t = db.table("players"); t && t->has("playerid")) {
        Snapshot s;
        if (s.load(db.memory(), *t)) {
            const Field* fl = t->field("lastnameid");
            const Field* fc = t->field("commonnameid");
            for (uint32_t i : s.valid) {
                int64_t cn = fc ? s.get_int(i, *fc) : 0, ln = fl ? s.get_int(i, *fl) : 0;
                if (cn > 0) ++index.name_users[cn];
                else if (ln > 0) ++index.name_users[ln];
            }
        }
    }
    // the fallback spoken set follows the names in the database
    if (!spoken.verified) spoken.ids = index.used_ids;
    for (const auto& kv : index.name_commentary) {
        if (!spoken.spoken(kv.second)) continue;
        NameChoice c;
        c.nameid = kv.first;
        c.commentaryid = kv.second;
        auto n = names.find(kv.first);
        c.name = n != names.end() ? n->second : ("name " + std::to_string(kv.first));
        auto u = index.name_users.find(kv.first);
        c.users = u != index.name_users.end() ? u->second : 0;
        index.names.push_back(std::move(c));
    }
    std::sort(index.names.begin(), index.names.end(), [](const NameChoice& a, const NameChoice& b) {
        std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.nameid < b.nameid;
    });
    for (const auto& kv : index.playernamemap) {
        if (!spoken.spoken(kv.second)) continue;
        PlayerChoice c;
        c.playerid = kv.first;
        c.commentaryid = kv.second;
        if (const PlayerRow* p = model.player(kv.first)) {
            c.name = p->name;
            c.club = p->club_name;
        } else {
            c.name = "player " + std::to_string(kv.first);
        }
        index.players.push_back(std::move(c));
    }
    std::sort(index.players.begin(), index.players.end(), [](const PlayerChoice& a, const PlayerChoice& b) {
        std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.playerid < b.playerid;
    });
    index.built = true;
}

SpokenAnswer Callnames::spoken_answer(int64_t cid) const {
    if (cid <= kNoCallname) return SpokenAnswer::Silent;
    if (spoken.spoken(cid) || masters.generic_ids.count(cid)) return SpokenAnswer::Spoken;
    // The spoken set answers for the range Turbo asks the game about (and a hand-made list holds only that range);
    // the fallback set is every id playernames uses, which says nothing about a playernamemap id
    if (cid <= kCallnameMax && spoken.verified) return SpokenAnswer::Silent;
    // Above 965000 the game is never asked: only the FC 26 list, which maps every recording of the language, can say no
    if (cid > kCallnameMax && masters.loaded()) return SpokenAnswer::Silent;
    return SpokenAnswer::Unknown;
}

std::string Callnames::silent_source(int64_t cid) const {
    if (cid <= kNoCallname) return "no callname";
    if (cid > kCallnameMax) return masters.label() + " (the game is not asked about ids above " + std::to_string(kCallnameMax) + ")";
    std::string game;
    switch (spoken.from) {
        case SpokenSet::From::ListFile: game = "your spoken-id list"; break;
        case SpokenSet::From::BankCapture: game = "the bank capture"; break;
        default: game = "the game's audio service"; break;
    }
    return masters.loaded() ? game + " and " + masters.label() : game;
}

SpareRow Callnames::spare_playernamemap_row(const std::vector<NameMapRow>& rows, const std::function<bool(int64_t)>& in_database,
                                            int64_t for_player) const {
    // One pass, the first row of each kind kept; the best kind wins (table order inside a kind)
    SpareRow best[3];
    SpareRow out;
    for (const NameMapRow& r : rows) {
        if (!r.rec || (r.playerid == for_player && for_player > 0)) continue;
        SpareRow s;
        s.rec = r.rec;
        s.playerid = r.playerid;
        s.commentaryid = r.commentaryid;
        // without a lookup a player's absence cannot be told: such rows are judged by their callname alone
        if (r.playerid <= 0 || (in_database && !in_database(r.playerid))) {
            s.why = SpareWhy::NoPlayer;
        } else if (r.commentaryid <= kNoCallname) {
            s.why = SpareWhy::NoCallname;
        } else {
            const SpokenAnswer a = spoken_answer(r.commentaryid);
            if (a == SpokenAnswer::Spoken) {
                ++out.spoken;
                continue;
            }
            if (a == SpokenAnswer::Unknown) {
                ++out.unknown;
                continue;
            }
            s.why = SpareWhy::NotSpoken;
        }
        const int k = s.why == SpareWhy::NoPlayer ? 0 : s.why == SpareWhy::NoCallname ? 1 : 2;
        if (!best[k].rec) best[k] = s;
    }
    for (const SpareRow& b : best)
        if (b.rec) {
            out.rec = b.rec;
            out.playerid = b.playerid;
            out.commentaryid = b.commentaryid;
            out.why = b.why;
            break;
        }
    return out;
}

CallnameInfo Callnames::resolve(const PlayerRow& p, Database& db) const {
    const Table* t = db.table("players");
    int64_t cn = t ? db.get_int(*t, p.rec, "commonnameid", 0) : 0;
    int64_t ln = t ? db.get_int(*t, p.rec, "lastnameid", 0) : 0;
    CallnameInfo info = resolve_callname(p.playerid, cn, ln, index.playernamemap, index.name_commentary);
    info.own = own_recording(p.playerid);
    info.real = info.own != 0;
    return info;
}

fs::path game_root_from_process() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
    return fs::path(buf).parent_path();
#else
    return {};
#endif
}

}  // namespace turbo
