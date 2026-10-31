// 3.1.1 brief m3 (defect D3, Cassio 2026-10-24 20:03: the GUI frames of one manager drew differently): ONE resolver decides which id
// the game is asked to draw. Header-only on purpose: every build file stays as it is, and only the two frames that hand an id to
// the capture (ui/ui_looks_studio.cpp, ui/ui_images.cpp) include it.
//   the user's manager     -> 9999 (kUserAvatarHeadId): the game's created avatar. The game's own manager-head builder 0x147D94218
//                              is called with [obj+0x48] = 9999 and [obj+0x4C] = the user's club, and sets descriptor +0x68 = (id == 9999)
//                              (docs/re/manager_rules.md section 6, [H]); plan_request (player_capture.cpp) reads the same 9999.
//   any other manager      -> his headassetid (heads_staff; whether the game renders it as +0x00 is untested, same doc)
//   a player               -> his playerid
// "The user's manager" = the manager row whose teamid is the user's club (core/reapply.h: the career identity), only inside a career.
// capture_cache_key: a cache of pictures belongs to the SUBJECT plus the MODE (camera, the mode override), never to a bare number:
// a player numbered 9999 and the user's manager (9999) are two subjects. Nothing caches a capture by id in the two frames today
// (the studio holds one picture, each Miniface editor its own); the key is the one a cache must use.
#pragma once
#include <cstdint>
#include <string>

#include "core/bridge.h"
#include "core/player_capture.h"

namespace turbo {
namespace capture {

struct CaptureSubject {
    bool manager = false;    // a manager (or staff head) rather than a player
    int64_t id = 0;          // playerid, or managerid
    int64_t head = 0;        // a manager's headassetid (0 = none)
    int64_t club = 0;        // his club (player.club / manager.teamid), 0 = none
    bool in_career = false;  // the bridge says a career is loaded
    int64_t user_team = 0;   // the user's club, 0 = unknown
};

// A subject, with the career facts taken from the bridge state (the one place that reads them for a capture)
inline CaptureSubject make_capture_subject(const BridgeState& bs, bool manager, int64_t id, int64_t head, int64_t club) {
    CaptureSubject s;
    s.manager = manager;
    s.id = id;
    s.head = head;
    s.club = club;
    s.in_career = bs.in_cm;
    s.user_team = bs.user_team;
    return s;
}

// The user's manager. A free agent (club 0) and every other club's manager are not.
// Two rules, either is enough (5.0c E9): (1) inside a career, the manager row whose teamid is the user's club; (2) the manager row whose
// own managerid is 9999 (the created avatar's id; seen in game on 10-24: Jamie Vardy, managerid 9999, club 95), which does not depend on the
// bridge having said in_cm / user_team yet.
inline bool is_user_manager(const CaptureSubject& s) {
    if (!s.manager) return false;
    if (s.id == static_cast<int64_t>(kUserAvatarHeadId)) return true;
    return s.in_career && s.user_team > 0 && s.club == s.user_team;
}

// THE capture id. 0 = nothing the game can draw (a manager without a head, a player without an id).
inline int64_t capture_subject_id(const CaptureSubject& s) {
    if (s.manager) return is_user_manager(s) ? static_cast<int64_t>(kUserAvatarHeadId) : (s.head > 0 ? s.head : 0);
    return s.id > 0 ? s.id : 0;
}

// subject + mode: `camera` is the Request's camera, `mode_override` the Request's (-1 = the camera's own mode)
inline std::string capture_cache_key(const CaptureSubject& s, int camera, int mode_override) {
    return std::string(s.manager ? "manager:" : "player:") + std::to_string(capture_subject_id(s)) + "|camera:" + std::to_string(camera) +
           "|mode:" + std::to_string(mode_override);
}

}  // namespace capture
}  // namespace turbo
