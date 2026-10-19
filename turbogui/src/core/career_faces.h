// FC 27 LE Turbo GUI - career faces (2.19): a face picture chosen in a career lives ONLY in that career.
//
// Cassio's rule (2026-10-18): changes made in a career live only in that career; changes made outside a career belong to the
// squad file. Before 2.19 the real-face chooser (and the miniface editor) wrote a career's choice straight over the GLOBAL
// <LE>\mods\legacy\data\ui\imgAssets\heads\p<pid>.dds, so a real player's picture changed in every career, Kick Off and the
// squad file.
//
// Why not a serve hook: FC 27 loads minifaces on the SYNC legacy path (TryFindFile + LoadFile, served by Live Editor from
// mods\legacy; Live Editor log 2026-10-18 CS218C: 146 of 149 heads/p<pid>.dds lookups followed by LoadFile), never through
// BeginLoadAsync, so the 2.18 crest serve never sees them. Live Editor looks at mods\legacy per call (a picture written mid
// session shows at once; removing it brings the game's own back: 2.14 Undo).
//
// The route: the picture is kept per career in
//     <LE>\turbo_output\careers\<career id>\heads\p<pid>.dds    (and heads_staff\..., the path under data/ui/imgAssets/)
// (career id = the 16 hex digits of core/reapply.h career_identity_id) and copied into mods\legacy ONLY while that career is
// identified (sync(career)). When the career is left, another one loads, or Turbo starts (sync("")), every copied file is put
// back the way it was: removed when there was none, the earlier file (a global custom picture) restored from its backup.
//
// The safety record <LE>\turbo_output\careers\active.json is written BEFORE every change in mods\legacy (write-ahead): per
// copied path its content hash and where the file it replaced was kept (careers\_base\...). A path is restored ONLY while
// mods\legacy still holds Turbo's copy (same FNV-1a 64 hash); a file someone else wrote over it is left alone (logged).
// An unreadable record is never acted on: it is moved aside (active.json.bad) and nothing in mods\legacy is touched.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbo {
namespace career_faces {

// The career scope is a 16-digit lower-case hex id (career_identity_id); anything else is no career
bool is_career_id(const std::string& id);
// A face picture this store keeps per career: a safe relative legacy path under data/ui/imgAssets/heads/ or heads_staff/,
// ending in .dds (any letter case), no "..", no drive, no backslash after normalising
bool is_face_path(const std::string& rel);

struct Paths {
    std::filesystem::path turbo_output;  // <LE>\turbo_output
    std::filesystem::path mods_legacy;   // <LE>\mods\legacy
};

std::filesystem::path careers_dir(const Paths& p);                                    // turbo_output\careers
std::filesystem::path manifest_file(const Paths& p);                                  // careers\active.json
std::filesystem::path overlay_file(const Paths& p, const std::string& career, const std::string& rel);  // careers\<id>\heads\p1.dds
std::filesystem::path mods_file(const Paths& p, const std::string& rel);              // mods\legacy\<rel>

// The career's own copy of a face picture (previous copy of THIS career kept for Undo in careers\<id>\_undo: `undo_backup` = that
// copy, empty when the career had none). Refused for an invalid career id or path, or no bytes.
bool save_overlay(const Paths& p, const std::string& career, const std::string& rel, const std::vector<uint8_t>& bytes,
                  std::string* err, std::filesystem::path* undo_backup);
// Removes the career's copy (kept for Undo the same way). False with err when there is none.
bool remove_overlay(const Paths& p, const std::string& career, const std::string& rel, std::string* err,
                    std::filesystem::path* undo_backup);
bool has_overlay(const Paths& p, const std::string& career, const std::string& rel);
// Puts a copy from save_overlay / remove_overlay back (empty backup = the career had none: its copy is removed)
bool restore_overlay(const Paths& p, const std::string& career, const std::string& rel, const std::filesystem::path& undo_backup,
                     std::string* err);

struct SyncResult {
    std::vector<std::string> copied;        // copied into mods\legacy for this career
    std::vector<std::string> restored;      // put back as it was before (removed, or the earlier file restored)
    std::vector<std::string> left_foreign;  // not Turbo's copy any more: left alone
    std::vector<std::string> errors;
    std::string career;                     // the career the record holds afterwards ("" = none)
    bool ok() const { return errors.empty(); }
    // every path whose mods\legacy file changed (copied + restored)
    std::vector<std::string> touched() const;
    std::string summary() const;  // one line for the log
};
// Makes mods\legacy hold exactly the face pictures of `career` ("" or not a career id = none): restores every recorded path
// that is not wanted any more (another career, no career, the career's copy removed or changed) and copies the wanted ones in.
SyncResult sync(const Paths& p, const std::string& career);
// The career the safety record says is in mods\legacy now ("" = none or no record)
std::string active_career(const Paths& p);

// ---------------------------------------------------------------- 2.20 faces safety (Cassio 2026-10-19 10:56)
// "EDITING PLAYER'S HEADS CANNOT OVERWRITE OR DELETE THE ORIGINAL, IN-GAME ASSET. importing 3d heads should NEVER override a real
// miniface, and should trigger an automatic 3d model miniface for players with no miniface at time of import."
//
// (a) Who owns a face file in mods\legacy. Turbo may replace or delete a face picture there ONLY when it is Turbo's own copy:
//     a career copy (active.json, same hash) or a picture Turbo wrote outside a career (careers\global_own.json, same hash).
//     Anything else (the donor set, a user's or another tool's file, a picture written by Turbo before 2.20, which has no hash
//     record) is left alone and the refusal says why.
// (b) The miniface of the target of a head given by the chooser: real (the game ships one, or a file Turbo did not make, or a
//     career copy over such a file) = never touched; unknown (no catalog and the game has not answered) = treated as real;
//     none / Turbo's own = the miniface is rendered from his new 3D head for the career only.
// (c) heads_own.json: the player's own head fields, stored once (the first original wins) before the first head write, per
//     career (careers\<id>\heads_own.json) or outside a career (careers\_squad\heads_own.json); "His own head" writes them back.

// FNV-1a 64 of bytes / of a file, 16 hex digits ("" = absent or unreadable)
std::string bytes_hash(const std::string& bytes);
std::string file_hash(const std::filesystem::path& f);

enum class Owner { None, TurboCareer, TurboGlobal, Foreign };
struct OwnerInfo {
    Owner owner = Owner::None;
    bool base_under = false;  // a career copy over an earlier file (kept in careers\_base): a real file is underneath
    std::string hash;         // of the file now ("" = none)
};
// Who owns mods\legacy\<rel> now (an unreadable safety record counts every present file as Foreign)
OwnerInfo owner_of(const Paths& p, const std::string& rel);

std::filesystem::path global_own_file(const Paths& p);  // careers\global_own.json
// May Turbo replace or delete mods\legacy\<rel> outside a career? Absent, or Turbo's own global copy. why = the refusal.
bool may_replace_global(const Paths& p, const std::string& rel, std::string* why);
// Records (before the write) that Turbo's global copy of rel has these bytes / forgets it (after a delete)
bool record_global(const Paths& p, const std::string& rel, const std::string& bytes, std::string* err);
bool forget_global(const Paths& p, const std::string& rel, std::string* err);

enum class Ships { Yes, No, Unknown };  // the game files' own miniface for the id (asset catalog, else Live Editor's answer)
enum class MinifaceKind { None, TurboOwn, Real, Unknown };
struct MinifaceFacts {
    OwnerInfo file;               // mods\legacy\<rel> (owner_of)
    Ships game = Ships::Unknown;  // the game's own picture
};
MinifaceKind classify_miniface(const MinifaceFacts& f);
const char* miniface_kind_name(MinifaceKind k);
inline bool keep_miniface(MinifaceKind k) { return k == MinifaceKind::Real || k == MinifaceKind::Unknown; }

// ---- heads_own.json
std::string squad_scope();  // "_squad": outside a career (the squad file's players)
bool is_head_scope(const std::string& scope);  // a career id or squad_scope()
std::filesystem::path heads_own_file(const Paths& p, const std::string& scope);
struct OwnHead {
    std::string name;
    std::string at;                         // when it was stored (local time)
    std::map<std::string, int64_t> fields;  // field -> his own value
    bool picture = false;                   // a miniface was rendered for him by a head give in this scope (career copy)
};
struct HeadsOwn {
    std::map<int64_t, OwnHead> players, managers;
};
// false = unreadable (never overwritten: head writes are refused until someone looks at it); a missing file is empty
bool load_heads_own(const Paths& p, const std::string& scope, HeadsOwn& out, std::string* err);
// First original wins: stores `fields` for the id unless the scope already has a record. true = a record is on disk now
// (`created` = this call wrote it). Refused for a bad scope, no fields or an unreadable file.
bool record_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, const std::string& name,
                     const std::map<std::string, int64_t>& fields, std::string* err, bool* created = nullptr);
bool find_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, OwnHead& out);
bool set_own_picture(const Paths& p, const std::string& scope, bool manager, int64_t id, bool picture, std::string* err);
bool drop_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, std::string* err);

}  // namespace career_faces
}  // namespace turbo
