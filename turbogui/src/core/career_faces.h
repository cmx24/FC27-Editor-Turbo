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

}  // namespace career_faces
}  // namespace turbo
