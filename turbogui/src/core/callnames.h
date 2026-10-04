// FC 27 LE Turbo GUI - spoken player names ("callnames") in the game's commentary.
//
// How the game decides what the commentators say for a player (FC 26/27, verified by the PT-BR callname project):
//   generic bank  playernames.commentaryid of the BINDING name id (commonnameid when set, else lastnameid),
//                 overridden per player by playernamemap (playerid -> commentaryid)
//   real bank     recorded names keyed by the player's own id inside the commentary audio banks
// Which of those ids actually have audio depends on the commentary LANGUAGE the game has loaded: every language pack
// was recorded separately. Turbo therefore
//   1. finds the commentary packs installed in the game folder (<game>\commentary\commentaryfull_<lang>.toc and
//      Data\Win32\commentaryfull_<lang>.toc), e.g. ita_it, eng_us, por_br
//   2. loads, per language, the list of spoken ids from turbo_output\callnames\<lang>*.csv (header-driven CSV:
//      a `commentaryid` column = generic bank, a `playerid` / `donor_playerid` column = real bank). The user's own
//      bank exports go there (one file per language); nothing is tied to one language.
//   3. without a list for the loaded language it falls back to the database's commentarynames table and says so
//      (that table lists every id the commentary system knows, not what a given language recorded).
// Commentary text (commentarynames.commentarystring, compressed in FC 27) comes from Turbo's Lua side through
// turbo_output\bridge_commentary.txt, like player names do through bridge_names.txt.
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "t3db.h"

namespace turbo {

constexpr int64_t kNoCommentary = 900000;  // playernames.commentaryid meaning "no callname" (the field's minimum)

struct CallnameLang {
    std::string code;          // ita_it, eng_us, por_br ...
    bool installed = false;    // a commentaryfull_<code>.toc exists in the game folder
    bool has_list = false;     // a turbo_output\callnames\<code>*.csv was read
    std::unordered_set<int64_t> generic;  // commentaryids with audio
    std::unordered_set<int64_t> real;     // playerids with a recorded name
    std::vector<std::filesystem::path> files;
};

// How a player is (or is not) called
struct CallnameInfo {
    int64_t binding_nameid = 0;    // commonnameid when set, else lastnameid
    const char* binding_field = "lastnameid";
    int64_t commentaryid = 0;      // from playernamemap (override) or playernames
    bool mapped = false;           // playernamemap row used
    bool generic_spoken = false;   // commentaryid has audio in the active language (or is known, see unverified)
    bool real_spoken = false;      // the player's own id is in the real bank of the active language
    bool unverified = false;       // no list for the language: "known to the database" only
    std::string text;              // commentary text (bridge_commentary.txt), may be empty
};

struct CallnameCandidate {
    bool real = false;        // false: a name id (generic bank); true: a player (real bank)
    int64_t id = 0;           // nameid or playerid
    int64_t commentaryid = 0; // generic: the id that plays
    std::string name;         // name text (nameid) or player name
    std::string text;         // commentary text
    std::string lname;        // lower-case search key
};

class Callnames {
public:
    explicit Callnames(std::filesystem::path le_root);

    void set_game_dir(const std::filesystem::path& p) { game_dir_ = p; }
    const std::filesystem::path& game_dir() const { return game_dir_; }
    std::filesystem::path lists_dir() const;  // turbo_output\callnames

    // Re-scan the game folder, the list files and bridge_commentary.txt (cheap; call on refresh / every few seconds)
    void scan();
    // Index the database tables (playernames, playernamemap, commentarynames); call after a database refresh
    void index_db(Database& db);
    bool indexed() const { return indexed_; }

    const std::vector<CallnameLang>& languages() const { return langs_; }
    // Language whose lists are used: the user's choice when installed, else the single/first installed pack
    const CallnameLang* active() const;
    const std::string& active_code() const { return active_; }
    void set_active(const std::string& code) { active_ = code; }
    std::vector<std::string> installed_codes() const;

    bool generic_spoken(int64_t commentaryid) const;
    bool real_spoken(int64_t playerid) const;
    bool known(int64_t commentaryid) const { return commentary_known_.count(commentaryid) > 0; }
    std::string commentary_text(int64_t commentaryid) const;
    int64_t commentaryid_of_name(int64_t nameid) const;    // playernames, 0 when unknown
    int64_t commentaryid_of_player(int64_t playerid) const;  // playernamemap, 0 when none
    size_t commentary_texts() const { return commentary_text_.size(); }

    CallnameInfo info(int64_t playerid, int64_t commonnameid, int64_t lastnameid) const;
    // Every name id / player whose callname plays in the active language (names from `name_of`)
    template <class NameOf, class PlayerNameOf>
    std::vector<CallnameCandidate> candidates(NameOf name_of, PlayerNameOf player_name_of) const {
        std::vector<CallnameCandidate> out;
        const CallnameLang* L = active();
        for (const auto& kv : name_commentary_) {
            if (kv.second <= kNoCommentary) continue;
            if (L && L->has_list ? L->generic.count(kv.second) == 0 : !known(kv.second)) continue;
            CallnameCandidate c;
            c.id = kv.first;
            c.commentaryid = kv.second;
            c.name = name_of(kv.first);
            c.text = commentary_text(kv.second);
            c.lname = lower(c.name + " " + c.text);
            out.push_back(std::move(c));
        }
        if (L && L->has_list) {
            for (int64_t pid : L->real) {
                CallnameCandidate c;
                c.real = true;
                c.id = pid;
                c.name = player_name_of(pid);
                c.lname = lower(c.name);
                out.push_back(std::move(c));
            }
        }
        std::sort(out.begin(), out.end(), [](const CallnameCandidate& a, const CallnameCandidate& b) {
            if (a.real != b.real) return !a.real;
            return a.lname < b.lname;
        });
        return out;
    }

    // Parsers (also used by tests)
    static bool parse_list_csv(const std::string& text, std::unordered_set<int64_t>& generic, std::unordered_set<int64_t>& real);
    static bool parse_commentary(const std::string& text, std::unordered_map<int64_t, std::string>& out);
    static std::string lower(std::string s);

private:
    CallnameLang& lang(const std::string& code);

    std::filesystem::path root_;
    std::filesystem::path game_dir_;
    std::vector<CallnameLang> langs_;
    std::string active_;
    std::unordered_map<int64_t, std::string> commentary_text_;
    std::unordered_map<int64_t, int64_t> name_commentary_;    // nameid -> commentaryid (playernames)
    std::unordered_map<int64_t, int64_t> player_commentary_;  // playerid -> commentaryid (playernamemap)
    std::unordered_set<int64_t> commentary_known_;            // commentarynames
    bool indexed_ = false;
    std::filesystem::file_time_type commentary_time_{};
    bool have_commentary_time_ = false;
    double last_scan_ = -1.0;
};

}  // namespace turbo
