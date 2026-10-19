// FC 27 LE Turbo GUI - player callnames for the loaded commentary language (see callnames.h and docs/callnames.md)
#include "callnames.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
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
    // 2.21 CHANGE 8: only the packs the game plays from, <game>\Data\Win32 (toc above stub size + a non-empty full cas);
    // <game>\commentary is Cassio's backup store and is never read
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(game_root / "Data" / "Win32", ec)) {
        std::error_code fec;
        if (e.is_directory(fec)) continue;
        const std::string code = pack_code(e.path().filename().string(), true);
        if (code.empty() || !launcher_pack_live(game_root, code, nullptr)) continue;
        bool dup = false;
        for (const auto& p : out) dup = dup || p.code == code;
        if (!dup) out.push_back({code});
    }
    std::sort(out.begin(), out.end(), [](const CommentaryPack& a, const CommentaryPack& b) { return a.code < b.code; });
    return out;
}

std::string commentary_language_log_line(const std::string& lang, const std::string& why) {
    return "callnames: language " + (lang.empty() ? std::string("none") : lang) + " (" + why + ")";
}

LanguageMatch match_loaded_language(const std::unordered_set<int64_t>& game_spoken, const std::vector<LanguageCandidate>& cands,
                                    const std::unordered_set<int64_t>* asked) {
    LanguageMatch m;
    const bool restrict = asked && !asked->empty();
    // only ids the game was asked about can be compared: a candidate id outside them is not evidence against it
    auto in_range = [&](int64_t id) { return id > kCallnameMin && id <= kCallnameMax && (!restrict || asked->count(id)); };
    size_t g = 0;
    for (int64_t id : game_spoken)
        if (in_range(id)) ++g;
    if (g == 0) {
        m.why = "the game's answer holds no commentary id";
        return m;
    }
    std::vector<std::pair<double, const LanguageCandidate*>> scores;
    std::string detail;
    for (const auto& c : cands) {
        size_t n = 0, both = 0;
        for (int64_t id : c.ids) {
            if (!in_range(id)) continue;
            ++n;
            if (game_spoken.count(id)) ++both;
        }
        if (n == 0) continue;
        const double j = double(both) / double(g + n - both);
        scores.push_back({j, &c});
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s%s %.2f (%s, %zu ids)", detail.empty() ? "" : ", ", c.lang.c_str(), j, c.from.c_str(), n);
        detail += buf;
    }
    if (scores.empty()) {
        m.why = "no language with a known id set to compare with";
        return m;
    }
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    m.score = scores[0].first;
    m.second = scores.size() > 1 ? scores[1].first : 0.0;
    const std::string head = "the game's audio service answered " + std::to_string(g) + " spoken ids" +
                             (restrict ? " of " + std::to_string(asked->size()) + " asked" : std::string()) + "; match: " + detail;
    if (m.score < kLangMatchMin) {
        m.why = head + " - no language matches (unknown)";
    } else if (scores.size() > 1 && m.score - m.second < kLangMatchMargin) {
        m.why = head + " - two languages match about as well (unknown)";
    } else {
        m.lang = scores[0].second->lang;
        m.why = head;
    }
    return m;
}

std::string detect_commentary_language(const std::string& override_lang, const std::string& runtime_lang, const std::string& runtime_why,
                                       const std::string& saved_lang_in, const std::string& saved_why, const RegistryLocale& reg,
                                       std::string* why, bool* proven, const LauncherHint* hint) {
    auto set = [&](const std::string& s) { if (why) *why = s; };
    if (proven) *proven = true;
    const std::string ov = lower(override_lang);
    const std::string hl = hint ? lower(hint->lang) : std::string();
    if (!ov.empty()) {
        set("MANUAL OVERRIDE in turbo_output\\gui_settings.json callnames.override_language - detection is off" +
            (hl.empty() ? std::string() : " (launcher hint " + hl + " not used: the manual override wins)"));
        return ov;
    }
    if (!runtime_lang.empty()) {
        std::string w = "detected in the running game: " + runtime_why;
        if (!hl.empty() && hl != lower(runtime_lang))
            w += " [launcher hint " + hl + " differs: the running game decides (the in-game Audio setting picks the commentary)]";
        set(w);
        return lower(runtime_lang);
    }
    std::string pre = runtime_why.empty() ? std::string("no runtime evidence yet") : "runtime: " + runtime_why;
    // 2.21 CHANGE 8: with a usable registry Locale the game offers English + that Locale's language only (PLAN H.1 (b));
    // an earlier answer outside those two was heard under another Locale and is not used
    auto offered = [&](const std::string& l) { return !reg.usable() || l == "eng_us" || l == reg.lang; };
    const std::string offer_note = reg.usable() ? "the registry Locale " + reg.locale + " offers English + " + reg.lang + " only" : std::string();
    std::string saved_lang = lower(saved_lang_in);
    if (!saved_lang.empty() && !offered(saved_lang)) {
        pre += "; earlier answer " + saved_lang + " not used: " + offer_note;
        saved_lang.clear();
    }
    // 2.21 launcher hint: after this session's runtime answer, before an earlier session's saved answer heard before it
    std::string hint_note;  // why the hint was not used ("" = no hint / used)
    if (!hl.empty()) {
        const std::string hs = normalize_stamp(hint->stamp);
        const std::string sw = normalize_stamp(hint->saved_when);
        if (!language_code_ok(hl)) {
            hint_note = "launcher hint " + hl + " ignored: not a language code";
        } else if (!hint->pack_live) {
            hint_note = "launcher hint " + hl + " ignored: its pack is not live in Data\\Win32 (" + hint->pack_why + ")";
        } else if (!saved_lang.empty() && lower(saved_lang) == hl) {
            set("heard in an earlier session (" + saved_why + "); launcher hint " + hl + " agrees; " + pre);
            return hl;
        } else if (!saved_lang.empty() && (hs.empty() || (!sw.empty() && sw > hs))) {
            hint_note = "launcher hint " + hl + " ignored: " +
                        (hs.empty() ? std::string("its stamp '" + hint->stamp + "' is not a date") : "set " + hs + ", before the answer heard " + sw);
        } else {
            if (proven) *proven = false;
            set("launcher hint " + hl + " (picked in the launcher" + (hs.empty() ? std::string() : " " + hs) +
                "; its pack is live in Data\\Win32); NOT PROVEN (automatic callname writes wait for the running game's answer); " + pre +
                (saved_lang.empty() ? std::string() : "; earlier answer " + saved_lang + " heard " + (sw.empty() ? "undated" : sw) + " not used") +
                (offered(hl) ? std::string() : " [" + offer_note + "]"));
            return hl;
        }
    }
    if (!hint_note.empty()) pre += "; " + hint_note;
    if (!saved_lang.empty()) {
        set("heard in an earlier session (" + saved_why + "); " + pre);
        return saved_lang;
    }
    if (proven) *proven = false;
    pre += "; " + (saved_why.empty() ? std::string("no saved answer") : "saved answer not used: " + saved_why);
    pre += "; game settings file: not read (field not proven); NOT PROVEN (automatic callname writes wait)";
    // step c: the registry Locale, its pack live in Data\Win32
    if (reg.usable()) {
        set("registry Locale " + reg.locale + ", pack present (" + reg.pack_why + "; " + reg.read_why + "); the game offers English + " + reg.lang +
            ", the in-game Audio setting picks one; " + pre);
        return reg.lang;
    }
    std::string rn;
    if (reg.locale.empty())
        rn = "registry Locale not read: " + (reg.read_why.empty() ? std::string("not found") : reg.read_why);
    else if (reg.lang.empty())
        rn = "registry Locale " + reg.locale + " is not in the game's locale table";
    else
        rn = "registry Locale " + reg.locale + " -> " + reg.lang + ", its pack is not live in Data\\Win32 (" + reg.pack_why + ")";
    set("eng_us, the base game's language (" + rn + "); " + pre);
    return "eng_us";
}

std::string commentary_lang_for_locale(const std::string& locale) {
    std::string l = lower(locale);
    for (auto& c : l)
        if (c == '-') c = '_';
    // FC27.exe locale table @0xa8ba164 (PLAN H.1 item 4): one commentary code per locale; eng,us is the base
    static const std::pair<const char*, const char*> kTable[] = {{"nl_nl", "dut_nl"}, {"it_it", "ita_it"}, {"fr_fr", "fre_fr"}, {"de_de", "ger_de"},
                                                                 {"pt_br", "por_br"}, {"es_es", "spa_es"}, {"en_us", "eng_us"}, {"en_gb", "eng_us"}};
    for (const auto& kv : kTable)
        if (l == kv.first) return kv.second;
    return "";
}

std::string read_registry_locale(std::string* why) {
#ifdef _WIN32
    // read-only: KEY_QUERY_VALUE, nothing is ever written
    auto read_view = [](REGSAM view, std::string& out) -> LONG {
        HKEY h = nullptr;
        LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EA Sports\\EA SPORTS FC 27", 0, KEY_QUERY_VALUE | view, &h);
        if (rc != ERROR_SUCCESS) return rc;
        wchar_t buf[64] = {};
        DWORD type = 0, size = sizeof(buf) - sizeof(wchar_t);
        rc = RegQueryValueExW(h, L"Locale", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
        RegCloseKey(h);
        if (rc != ERROR_SUCCESS) return rc;
        if (type != REG_SZ && type != REG_EXPAND_SZ) return ERROR_INVALID_DATA;
        out.clear();
        for (size_t i = 0; i < size / sizeof(wchar_t) && buf[i]; ++i) {
            if (buf[i] > 127) return ERROR_INVALID_DATA;
            out += static_cast<char>(buf[i]);
        }
        return out.empty() ? ERROR_INVALID_DATA : ERROR_SUCCESS;
    };
    std::string v64, v32;
    const LONG r64 = read_view(KEY_WOW64_64KEY, v64), r32 = read_view(KEY_WOW64_32KEY, v32);
    const std::string key = "HKLM\\SOFTWARE\\EA Sports\\EA SPORTS FC 27 Locale";
    if (r64 == ERROR_SUCCESS) {
        if (why)
            *why = key + " " + v64 +
                   (r32 != ERROR_SUCCESS ? std::string(" (64-bit view; 32-bit view: none)")
                                         : v32 == v64 ? std::string(" (both views)") : " (64-bit view; the 32-bit view says " + v32 + ")");
        return v64;
    }
    if (r32 == ERROR_SUCCESS) {
        if (why) *why = key + " " + v32 + " (32-bit view only)";
        return v32;
    }
    if (why) *why = key + ": not found (codes " + std::to_string(r64) + "/" + std::to_string(r32) + ")";
    return "";
#else
    if (why) *why = "no registry on this platform";
    return "";
#endif
}

std::string pack_data_source(const fs::path& game_root, const std::string& lang) {
    const std::string l = lower(lang);
    if (game_root.empty()) return "none (game folder unknown)";
    if (!language_code_ok(l)) return "none (not a language code)";
    std::error_code ec;
    const std::string name = std::string(kPackPrefix) + l;
    // 1. Cassio's backup store: toc above stub size + a non-empty cas in the folder named like the toc
    const fs::path btoc = game_root / "commentary" / (name + ".toc");
    if (fs::is_regular_file(btoc, ec) && fs::file_size(btoc, ec) >= kLiveTocMin && !ec) {
        for (const auto& e : fs::directory_iterator(game_root / "commentary" / name, ec)) {
            std::error_code fec;
            const std::string n = lower(e.path().filename().string());
            if (n.rfind("cas_", 0) == 0 && n.size() >= 8 && n.compare(n.size() - 4, 4, ".cas") == 0 && e.is_regular_file(fec) && e.file_size(fec) > 0 && !fec)
                return "backup store <game>\\commentary\\" + name;
        }
    }
    // 2. the live pack the game plays from
    std::string why;
    if (launcher_pack_live(game_root, l, &why)) return "Data\\Win32 " + name;
    return "none (no full pack for " + l + " in <game>\\commentary or Data\\Win32)";
}

LocaleReader& locale_reader_for_tests() {
    static LocaleReader r;
    return r;
}

RegistryLocale registry_locale_from(const std::string& locale, const std::string& read_why, const fs::path& game_root) {
    RegistryLocale r;
    r.locale = locale;
    r.read_why = read_why;
    r.lang = commentary_lang_for_locale(locale);
    if (!r.lang.empty()) r.pack_live = launcher_pack_live(game_root, r.lang, &r.pack_why);
    return r;
}

fs::path lang_detect_off_path(const fs::path& le_root) { return le_root / "turbo_output" / "callnames_lang_detect_off.txt"; }

bool language_code_ok(const std::string& lang) {
    if (lang.size() != 6 || lang[3] != '_') return false;
    for (size_t i = 0; i < lang.size(); ++i)
        if (i != 3 && !std::islower(static_cast<unsigned char>(lang[i]))) return false;
    return true;
}

std::string normalize_stamp(const std::string& s) {
    // YYYY-MM-DD[ T]HH:MM[:SS], anything after it (fraction, zone) ignored
    auto digits = [&](size_t at, size_t n) {
        if (at + n > s.size()) return false;
        for (size_t i = at; i < at + n; ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        return true;
    };
    if (!digits(0, 4) || s.size() < 16 || s[4] != '-' || !digits(5, 2) || s[7] != '-' || !digits(8, 2) || (s[10] != ' ' && s[10] != 'T') ||
        !digits(11, 2) || s[13] != ':' || !digits(14, 2))
        return "";
    std::string out = s.substr(0, 10) + " " + s.substr(11, 5);
    out += (s.size() >= 19 && s[16] == ':' && digits(17, 2)) ? s.substr(16, 3) : std::string(":00");
    return out;
}

bool launcher_pack_live(const fs::path& game_root, const std::string& lang, std::string* why) {
    auto say = [&](const std::string& s) { if (why) *why = s; return false; };
    const std::string l = lower(lang);
    if (game_root.empty()) return say("game folder unknown");
    if (!language_code_ok(l)) return say("not a language code");
    const fs::path w32 = game_root / "Data" / "Win32";
    std::error_code ec;
    const fs::path toc = w32 / (std::string(kPackPrefix) + l + ".toc");
    if (!fs::is_regular_file(toc, ec)) return say("no Data\\Win32\\" + toc.filename().string());
    const uintmax_t ts = fs::file_size(toc, ec);
    if (ec || ts < kLiveTocMin) return say("Data\\Win32\\" + toc.filename().string() + " is " + std::to_string(ec ? 0 : ts) + " B, a not-installed stub");
    const fs::path cas_dir = w32 / "superbundlelayout" / (std::string(kPackPrefix) + l);
    size_t cas = 0;
    uintmax_t bytes = 0;
    for (const auto& e : fs::directory_iterator(cas_dir, ec)) {
        std::error_code fec;
        const std::string n = lower(e.path().filename().string());
        if (n.rfind("cas_", 0) != 0 || n.size() < 8 || n.compare(n.size() - 4, 4, ".cas") != 0 || !e.is_regular_file(fec)) continue;
        const uintmax_t sz = e.file_size(fec);
        if (fec || sz == 0) continue;
        ++cas;
        bytes += sz;
    }
    if (cas == 0) return say("no non-empty cas file in Data\\Win32\\superbundlelayout\\" + cas_dir.filename().string());
    if (why) *why = "Data\\Win32 toc " + std::to_string(ts) + " B + " + std::to_string(cas) + " cas (" + std::to_string(bytes / (1024 * 1024)) + " MB)";
    return true;
}

fs::path loaded_language_path(const fs::path& le_root) { return le_root / "turbo_output" / "callnames" / "loaded_language.json"; }

std::string packs_fingerprint(const std::vector<CommentaryPack>& packs) {
    std::vector<std::string> v;
    for (const auto& p : packs) v.push_back(lower(p.code));
    std::sort(v.begin(), v.end());
    std::string out;
    for (const auto& x : v) out += (out.empty() ? "" : ",") + x;
    return out;
}

std::string saved_language_json(const SavedLanguage& s) {
    nlohmann::json j = {{"turbo", "loaded_language"}, {"lang", s.lang}, {"build", s.build}, {"packs", s.packs}, {"when", s.when}, {"why", s.why}};
    return j.dump(2) + "\n";
}

bool parse_saved_language_json(const std::string& text, SavedLanguage& out) {
    out = SavedLanguage{};
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("turbo", std::string()) != "loaded_language") return false;
    auto str = [&](const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
    out.lang = lower(str("lang"));
    out.build = str("build");
    out.packs = str("packs");
    out.when = str("when");
    out.why = str("why");
    return !out.lang.empty();
}

bool saved_language_usable(const SavedLanguage& s, const std::string& build, const std::string& packs_fp, std::string* why) {
    if (s.lang.empty()) {
        if (why) *why = "none saved";
        return false;
    }
    if (s.packs != packs_fp) {
        if (why) *why = s.lang + " was heard with other commentary packs (" + s.packs + ", now " + packs_fp + ")";
        return false;
    }
    if (!s.build.empty() && !build.empty() && s.build != build) {
        if (why) *why = s.lang + " was heard on game build " + s.build + ", now " + build;
        return false;
    }
    if (why) *why = s.lang + " heard " + (s.when.empty() ? std::string("earlier") : s.when) + " on build " + (s.build.empty() ? std::string("?") : s.build) +
                    " with the same packs" + (build.empty() || s.build.empty() ? " (build not compared: unknown)" : "");
    return true;
}

CaptureTarget capture_target(bool detection_off, const std::string& runtime_lang, const std::string& runtime_why, const std::string& override_lang,
                             const std::string& lang) {
    CaptureTarget t;
    if (detection_off) {
        t.cache_lang = lang;
        t.adopt = !lang.empty();
        return t;
    }
    if (runtime_lang.empty()) {
        t.note = "the game's answer matches no known language clearly, so it is not cached under any language and not used (" + runtime_why + ")";
        return t;
    }
    t.cache_lang = lower(runtime_lang);
    const std::string ov = lower(override_lang);
    if (!ov.empty() && ov != t.cache_lang) {
        t.note = "CONFLICT: manual override " + ov + " but the game loaded " + t.cache_lang + ": cached as spoken_" + t.cache_lang +
                 ".json, not used while the override is on";
        return t;
    }
    t.adopt = (lower(lang) == t.cache_lang);
    if (!t.adopt) t.note = "the language in use (" + lang + ") is not the one the game loaded (" + t.cache_lang + "): cached, not used";
    return t;
}

CallnamePopulation callname_population(const CatalogBank& b, const MasterList& m, const CallnameIndex& ix) {
    CallnamePopulation p;
    p.bank_generic = b.generic.size();
    p.bank_own = b.own.size();
    std::unordered_set<int64_t> db;
    for (int64_t id : ix.used_ids)
        if (id != kNoCallname && id > 0) db.insert(id);
    p.db_generic = db.size();
    std::unordered_set<int64_t> spec;
    for (const auto& kv : ix.playernamemap)
        if (kv.second != kNoCallname && kv.second > 0) spec.insert(kv.second);
    p.db_specific = spec.size();
    p.master_generic = m.generic_ids.size();
    p.master_own = m.real_players.size();
    std::unordered_set<int64_t> ug(b.generic.begin(), b.generic.end());
    ug.insert(db.begin(), db.end());
    ug.insert(spec.begin(), spec.end());
    ug.insert(m.generic_ids.begin(), m.generic_ids.end());
    p.union_generic = ug.size();
    std::unordered_set<int64_t> uo(m.real_players.begin(), m.real_players.end());
    for (const auto& kv : b.own) uo.insert(kv.first);
    for (int64_t id : b.own_other_team) uo.insert(id);
    p.union_own = uo.size();
    return p;
}

std::string callname_population_line(const CallnamePopulation& p) {
    auto n = [](size_t v) { return std::to_string(v); };
    return "bank " + n(p.bank_generic) + " generic / " + n(p.bank_own) + " own | DB " + n(p.db_generic) + " name ids / " + n(p.db_specific) +
           " player-specific | masters " + n(p.master_generic) + " generic / " + n(p.master_own) + " own | union " + n(p.union_generic) +
           " generic / " + n(p.union_own) + " own";
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
        if (auto it = j.find("real_simple_players"); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (id_of(v, id)) out.real_simple_players.insert(id);
        if (auto it = j.find("real_link_players"); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (id_of(v, id)) out.real_link_players.insert(id);
        // an FC 27 master always lists real_players; a file with only the two banks gets their union
        if (j.find("real_players") == j.end()) {
            out.real_players.insert(out.real_simple_players.begin(), out.real_simple_players.end());
            out.real_players.insert(out.real_link_players.begin(), out.real_link_players.end());
        }
        if (auto it = j.find("generic_names"); it != j.end() && it->is_object())
            for (auto n = it->begin(); n != it->end(); ++n) {
                if (!n.value().is_string()) continue;
                char* stop = nullptr;
                long long cid = std::strtoll(n.key().c_str(), &stop, 10);
                if (stop && *stop == '\0' && cid > 0) out.generic_names[static_cast<int64_t>(cid)] = n.value().get<std::string>();
            }
        if (auto it = j.find("names"); it != j.end() && it->is_object())
            for (auto n = it->begin(); n != it->end(); ++n) {
                if (!n.value().is_string()) continue;
                char* stop = nullptr;
                long long pid = std::strtoll(n.key().c_str(), &stop, 10);
                if (stop && *stop == '\0' && pid > 0) out.names[static_cast<int64_t>(pid)] = n.value().get<std::string>();
            }
        parse_master_audio(j, out.audio);  // "wav_dir" and "segments" (play buttons)
        if (out.real_players.empty() && out.generic_ids.empty()) return fail("the list holds no player or commentary ids");
        out.lang = out.lang.empty() ? lang : lower(out.lang);
        return true;
    } catch (const std::exception& e) {
        return fail(std::string("unreadable: ") + e.what());
    }
}

std::string own_recording_source_name(int own, SpokenSet::From game_from, const std::string& masters) {
    const char* game = game_from == SpokenSet::From::BankCapture ? "the bank capture"
                       : game_from == SpokenSet::From::Catalog   ? "the game's commentary files"
                                                                 : "the game's audio service";
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

std::vector<AllCallnameRow> all_callnames(const MasterList& m, const CallnameIndex& ix,
                                          const std::function<std::string(int64_t)>& player_name) {
    std::vector<AllCallnameRow> gen, own;
    std::unordered_map<int64_t, size_t> at;  // commentary id -> index in gen
    gen.reserve(m.generic_ids.size());
    for (int64_t cid : m.generic_ids) {
        AllCallnameRow r;
        r.commentaryid = cid;
        auto t = m.generic_names.find(cid);
        if (t != m.generic_names.end()) r.text = t->second;
        at[cid] = gen.size();
        gen.push_back(std::move(r));
    }
    for (const auto& [nameid, cid] : ix.name_commentary) {
        auto it = at.find(cid);
        if (it == at.end()) continue;
        AllCallnameRow& r = gen[it->second];
        auto u = ix.name_users.find(nameid);
        const int users = u != ix.name_users.end() ? u->second : 0;
        ++r.name_rows;
        r.users += users;
        // the name row most players use (the lowest id on a tie) carries the assignment
        if (!r.nameid) {
            r.nameid = nameid;
        } else {
            auto bu = ix.name_users.find(r.nameid);
            const int best = bu != ix.name_users.end() ? bu->second : 0;
            if (users > best || (users == best && nameid < r.nameid)) r.nameid = nameid;
        }
    }
    for (auto& r : gen)
        if (r.text.empty())
            for (const auto& c : ix.names)
                if (c.nameid == r.nameid) r.text = c.name;
    std::sort(gen.begin(), gen.end(), [](const AllCallnameRow& a, const AllCallnameRow& b) {
        const std::string la = lower(a.text), lb = lower(b.text);
        return la != lb ? la < lb : a.commentaryid < b.commentaryid;
    });
    own.reserve(m.real_players.size());
    for (int64_t pid : m.real_players) {
        AllCallnameRow r;
        r.playerid = pid;
        r.text = player_name ? player_name(pid) : std::string();
        if (r.text.empty()) {
            auto n = m.names.find(pid);
            r.text = n != m.names.end() ? n->second : std::to_string(pid);
        }
        own.push_back(std::move(r));
    }
    std::sort(own.begin(), own.end(), [](const AllCallnameRow& a, const AllCallnameRow& b) {
        const std::string la = lower(a.text), lb = lower(b.text);
        return la != lb ? la < lb : a.playerid < b.playerid;
    });
    gen.insert(gen.end(), std::make_move_iterator(own.begin()), std::make_move_iterator(own.end()));
    return gen;
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
    prev_lang = lang;
    override_lang = lower(chosen);
    {
        std::error_code oec;
        const bool was_off = runtime_off;
        runtime_off = fs::exists(lang_detect_off_path(le_root), oec);
        if (was_off && !runtime_off && runtime_lang.empty() && runtime_why.rfind("runtime detection off", 0) == 0) {
            runtime_why.clear();  // the switch was removed: no stale "off" reason, the game is asked again
            runtime_checked = false;
        }
    }
    // step a2: the answer heard in an earlier session, while the build and the packs are the same
    saved_lang.clear();
    saved_why.clear();
    launcher_hint.saved_when.clear();
    {
        std::string stext;
        SavedLanguage sv;
        if (read_text(loaded_language_path(le_root), stext) && parse_saved_language_json(stext, sv)) {
            if (saved_language_usable(sv, game_build, packs_fingerprint(packs), &saved_why)) {
                saved_lang = sv.lang;
                launcher_hint.saved_when = sv.when;
            }
        }
    }
    // 2.21: the launcher hint's pack, checked in Data\Win32 only (never <game>\commentary, Cassio's backup store)
    launcher_hint.pack_live = false;
    launcher_hint.pack_why.clear();
    if (!launcher_hint.lang.empty()) launcher_hint.pack_live = launcher_pack_live(game_root, launcher_hint.lang, &launcher_hint.pack_why);
    // 2.21 CHANGE 8: step c, the registry Locale (read-only), its pack checked in Data\Win32 only
    {
        std::string rw;
        const LocaleReader& tr = locale_reader_for_tests();
        const std::string loc = tr ? tr(&rw) : read_registry_locale(&rw);
        reg_locale = registry_locale_from(loc, rw, game_root);
    }
    lang_proven = false;
    if (no_game_root && override_lang.empty() && runtime_lang.empty()) {
        lang.clear();  // no game: nothing to detect (tests, a host without the game folder)
        lang_why = "game folder unknown: no commentary language detected";
    } else {
        lang = detect_commentary_language(override_lang, runtime_lang, runtime_why, saved_lang, saved_why, reg_locale, &lang_why, &lang_proven,
                                          launcher_hint.lang.empty() ? nullptr : &launcher_hint);
        // with the kill switch the registry Locale / eng_us answer is taken as before the runtime check (the user turned it off)
        // (never a launcher-hint answer: the hint is intent, not evidence; 2.21 CHANGE 7)
        if (!lang_proven && runtime_off && lang_why.rfind("launcher hint ", 0) != 0) {
            lang_proven = true;
            lang_why += " [runtime detection off: this answer is used as before]";
        }
    }
    // the data source of the active language's pack (backup store first, then Data\Win32): named, never the decision
    pack_data = lang.empty() ? std::string() : pack_data_source(game_root, lang);
    if (!pack_data.empty()) lang_why += " [pack data: " + pack_data + "]";
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
    // the live capture's players (bank capture / audio service cache): they pick the commentary team in automatic mode
    capture_players.clear();
    if (spoken.players_from == SpokenSet::From::GameAudio || spoken.players_from == SpokenSet::From::BankCapture)
        for (const auto& kv : spoken.players) capture_players.insert(kv.first);
    apply_bank();  // a current asset catalog's bank beats all of the above
    refreshed = true;
}

std::vector<LanguageCandidate> Callnames::language_candidates(const fs::path& le_root) const {
    std::vector<std::string> langs;
    auto add = [&](const std::string& l) {
        if (l.empty() || std::find(langs.begin(), langs.end(), l) != langs.end()) return;
        langs.push_back(l);
    };
    for (const auto& p : packs) add(p.code);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(le_root / "turbo" / "callnames" / "masters", ec)) {
        const std::string n = lower(e.path().filename().string());
        if (n.size() == 11 && n.substr(6) == ".json") add(n.substr(0, 6));
    }
    for (const auto& e : fs::directory_iterator(le_root / "turbo" / "callnames", ec)) {
        const std::string n = lower(e.path().filename().string());
        if (n.size() == 17 && n.rfind("spoken_", 0) == 0 && n.substr(13) == ".txt") add(n.substr(7, 6));
    }
    std::sort(langs.begin(), langs.end());
    std::vector<LanguageCandidate> out;
    for (const auto& l : langs) {
        LanguageCandidate c;
        c.lang = l;
        if (catalog) {
            CatalogBank b = catalog_bank(*catalog, l, "");
            if (!b.generic.empty()) {
                c.ids = b.generic;
                c.from = "asset catalog bank";
            }
        }
        std::string text;
        if (c.ids.empty() && read_text(master_list_path(le_root, l), text)) {
            MasterList m;
            if (parse_master_list_json(text, l, m)) {
                c.ids = m.generic_ids;
                c.from = m.label();
            }
        }
        if (c.ids.empty() && read_text(spoken_list_path(le_root, l), text)) {
            std::unordered_set<int64_t> ids;
            std::string fl;
            if (parse_spoken_list(text, ids, &fl) && (fl.empty() || lower(fl) == l)) {
                c.ids = std::move(ids);
                c.from = "spoken list";
            }
        }
        if (!c.ids.empty()) out.push_back(std::move(c));
    }
    return out;
}

bool Callnames::detect_runtime_language(const std::unordered_set<int64_t>& game_spoken, const fs::path& le_root,
                                        const std::unordered_set<int64_t>* asked) {
    std::error_code ec;
    if (fs::exists(lang_detect_off_path(le_root), ec)) {
        runtime_why = "runtime detection off (turbo_output\\callnames_lang_detect_off.txt)";
        runtime_checked = true;
        runtime_off = true;
        return false;
    }
    return accept_runtime_match(match_loaded_language(game_spoken, language_candidates(le_root), asked), le_root);
}

bool Callnames::accept_runtime_match(const LanguageMatch& m, const fs::path& le_root) {
    runtime_checked = true;
    const bool changed = m.lang != runtime_lang;
    runtime_lang = m.lang;
    runtime_why = m.why;
    if (!m.lang.empty()) {
        // saved for the next sessions (step a2): until the game answers again they start from this answer
        SavedLanguage sv;
        sv.lang = m.lang;
        sv.build = game_build;
        sv.packs = packs_fingerprint(packs);
        char buf[32] = {0};
        const std::time_t t = std::time(nullptr);
        if (const std::tm* tmv = std::localtime(&t)) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmv);
        sv.when = buf;
        sv.why = m.why;
        const fs::path p = loaded_language_path(le_root);
        std::error_code cec;
        fs::create_directories(p.parent_path(), cec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (f) f << saved_language_json(sv);
    }
    if (changed || (!m.lang.empty() && m.lang != lang) || (!m.lang.empty() && !lang_proven && override_lang.empty())) {
        refreshed = false;  // spoken set, masters, bank and the pickers follow the language on the next use
        index.built = false;
        return true;
    }
    return false;
}

void Callnames::use_catalog(std::shared_ptr<const assets::Catalog> cat) {
    catalog = std::move(cat);
    refreshed = false;     // the spoken set and the pickers' index are rebuilt on the next use (ensure_ready)
    index.built = false;
}

void Callnames::set_bank_team(const std::string& setting) {
    if (setting == bank_team_setting) return;
    bank_team_setting = setting;
    refreshed = false;
    index.built = false;
}

std::string Callnames::resolve_bank_team(std::string* why) const {
    auto say = [why](const std::string& w) {
        if (why) *why = w;
    };
    say("");
    if (!catalog || !catalog->loaded() || lang.empty()) return "";
    const std::vector<std::string> teams = catalog->callname_teams(lang);
    if (teams.size() <= 1) return teams.empty() ? std::string() : teams.front();  // a single-team pack (ita_it: "")
    if (bank_team_setting == "*") {
        say("your choice: a recording by any team counts");
        return "";
    }
    if (!bank_team_setting.empty() && std::find(teams.begin(), teams.end(), bank_team_setting) != teams.end()) {
        say("your choice");
        return bank_team_setting;
    }
    if (!capture_players.empty()) {
        // the captured players (asked from the bank the game loaded) against each team's SIMPLE / LINK recordings: the
        // team that alone records most of them is the loaded one
        std::vector<std::unordered_set<int64_t>> sets(teams.size());
        for (size_t i = 0; i < teams.size(); ++i)
            for (assets::CallKind k : {assets::CallKind::Simple, assets::CallKind::Link})
                for (int64_t id : catalog->callname_ids(lang, teams[i], k)) sets[i].insert(id);
        std::vector<size_t> only(teams.size(), 0), any(teams.size(), 0);
        for (int64_t pid : capture_players) {
            size_t n = 0, last = 0;
            for (size_t i = 0; i < teams.size(); ++i)
                if (sets[i].count(pid)) ++n, ++any[i], last = i;
            if (n == 1) ++only[last];
        }
        size_t best = 0;
        for (size_t i = 1; i < teams.size(); ++i)
            if (only[i] > only[best] || (only[i] == only[best] && any[i] > any[best])) best = i;
        bool unique = true;
        for (size_t i = 0; i < teams.size(); ++i)
            if (i != best && only[i] == only[best] && any[i] == any[best]) unique = false;
        if (any[best] > 0 && unique) {
            say("matches the live capture: " + std::to_string(any[best]) + " of its " + std::to_string(capture_players.size()) +
                " players with their own recording are recorded by " + teams[best] + ", " + std::to_string(only[best]) +
                " of them only by " + teams[best]);
            return teams[best];
        }
    }
    if (std::find(teams.begin(), teams.end(), "dr") != teams.end()) {
        say(capture_players.empty() ? "default: dr (Derek Rae, the team checked in game); no live capture to tell"
                                    : "default: dr (Derek Rae, the team checked in game); the live capture does not tell");
        return "dr";
    }
    say("default: the first team of the language");
    return teams.front();
}

void Callnames::apply_bank() {
    if (!catalog) {
        bank_team.clear();
        bank_team_why.clear();
        use_bank(CatalogBank{});
        return;
    }
    bank_team = resolve_bank_team(&bank_team_why);
    use_bank(catalog_bank(*catalog, lang, bank_team));
}

void Callnames::use_bank(CatalogBank b) {
    bank = std::move(b);
    if (!bank.loaded() || !bank.current) return;  // a stale catalog only adds rows to All callnames
    const bool had_list = spoken.from == SpokenSet::From::ListFile;
    spoken.verified = true;
    spoken.from = SpokenSet::From::Catalog;
    spoken.ids = bank.generic;
    spoken.players.clear();
    // only SIMPLE / LINK: the kick-off GetCallname check asks PLAYER_LOW_SIMPLE / _LINK, never the HIGH set, so a
    // HIGH-only player is not "spoken by his own recording" (the 2.0 SIMPLE|LINK rule of the FC 27 masters)
    size_t high_only = 0;
    for (const auto& kv : bank.own) {
        if (kv.second & kBankSpoken)
            spoken.players[kv.first] = kv.second;
        else
            ++high_only;
    }
    spoken.players_from = SpokenSet::From::Catalog;
    spoken.players_checked = bank.own.size();
    spoken.source = "the game's commentary files (asset catalog " + bank.build_key + ", " + bank.where() + "): " +
                    std::to_string(bank.generic.size()) + " generic ids, " + std::to_string(spoken.players.size()) +
                    " players with their own recording";
    if (high_only) spoken.source += " (" + std::to_string(high_only) + " more with only a high-intensity recording: not counted)";
    if (had_list) spoken.source += "; your spoken-id list is not used while the catalog is current";
}

bool Callnames::cache_capture(const BankCapture& c, const fs::path& le_root, const std::string& cache_lang, const std::string& when,
                              const std::string& build, std::string* err) {
    if (cache_lang.empty() || !c.ok) {
        if (err) *err = cache_lang.empty() ? "no language to cache it under" : (c.note.empty() ? "the capture found nothing" : c.note);
        return false;
    }
    const fs::path p = spoken_cache_path(le_root, cache_lang);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot write " + p.string();
        return false;
    }
    f << bank_cache_json(c, cache_lang, when, build);
    return true;
}

bool Callnames::apply_runtime_capture(const BankCapture& c, const fs::path& le_root, const std::string& when, const std::string& build,
                                      std::string* err, std::string* note) {
    std::error_code ec;
    const bool off = runtime_off || fs::exists(lang_detect_off_path(le_root), ec);
    const CaptureTarget t = capture_target(off, runtime_lang, runtime_why, override_lang, lang);
    if (note) *note = t.note;
    if (t.cache_lang.empty()) {
        if (err) *err = t.note.empty() ? "no commentary language detected" : t.note;
        return false;
    }
    if (t.adopt) return apply_capture(c, le_root, when, build, err);
    return cache_capture(c, le_root, t.cache_lang, when, build, err);
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
    if (spoken.from == SpokenSet::From::Catalog) {
        // cached; the game's commentary files (a current asset catalog) stay the spoken set, but in automatic mode the
        // new capture may name another commentary team: then the next refresh re-reads it and switches
        capture_players.clear();
        for (const auto& kv : cache.players) capture_players.insert(kv.first);
        if (resolve_bank_team() != bank_team) {
            refreshed = false;
            index.built = false;
        }
        return true;
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
    if (spoken.from == SpokenSet::From::Catalog) return spoken.spoken(cid) ? SpokenAnswer::Spoken : SpokenAnswer::Silent;
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
    if (spoken.from == SpokenSet::From::Catalog) return "the game's commentary files (asset catalog " + bank.build_key + ")";
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
