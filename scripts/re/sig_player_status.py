"""Make + check the player status / squad role / morale / block-offers signatures (docs/re/player_status_roles.md).

    python scripts/re/sig_player_status.py            # print signatures, uniqueness, resolved targets
    python scripts/re/sig_player_status.py --json     # also rewrite scripts/re/player_status_signatures.json
    python scripts/re/sig_player_status.py --check    # re-read the JSON and prove every pattern is unique and resolves

Same scheme as scripts/re/sig_transfer_lists.py: every anchor is a function start (or an instruction inside a
constructor / function whose rip-relative operand is the vtable or data address wanted) of FC27.exe 1.0.140.64835.
"rip" entries resolve the rip-relative operand at anchor + offset.  JSON format = transfer_list_signatures.json
(pattern / resolve / offset / note / va / target) so Turbo's sigscan table accepts it unchanged.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402
from sig_jobs import make_sig, find  # noqa: E402

BUILD = "6AB9813C-211EF000"
HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "player_status_signatures.json")

# name, anchor VA, resolve, rip offset, expected target (0 = the anchor itself), note
ANCHORS = [
    # ---- Block Offers (transfer block) --------------------------------------------------------------------
    ("uah_toggle_transfer_block", 0x147F688B8, "none", 0, 0,
     "void UserActionsHandlingHelperImpl::ToggleTransferBlock(this, int playerId) 0x147F688B8, UAH vtable slot 30 (0x14B029530): "
     "[[this+8]+0xFF8]=TM, TM+0x2D38 cached block dao: vf[1]=IsBlocked(pid) -> vf[7] unblock / else vf[4] block + RemoveFromLists + "
     "offers erase, then posts TransferBlockedByUserEvent (id 0xBE, +0x18 pid, +0x1C blocked-now). Same locate path as the list helpers."),
    ("tm_is_in_block_list", 0x147C471EC, "none", 0, 0,
     "bool TransferManager::IsInBlockList(tm, int playerId, uint8 flag) 0x147C471EC: eastl vector at TM+0x2F50 (begin) / +0x2F58 (end) of "
     "8-byte entries {int32 playerId, u8 flag}; flag 0 = Block Offers (user), flag 1 = released-player block"),
    ("cachedblock_vtable", 0x147C27073, "rip", 3, 0x14B005400,
     "CachedTransferblockDaoImpl vtable (0x14B005400): lea rcx,[rip+..] at +3 of 0x147C27073 in the TransferManager ctor 0x147C26450 "
     "(object 0x50 bytes stored at TM+0x2D38; +8 inner TransferblockDaoImpl, +0x10/+0x18 vector A (flag 0), +0x30/+0x38 vector B (flag 1))"),
    ("blockdao_vtable", 0x147C27013, "rip", 0, 0x14B005630,
     "TransferblockDaoImpl vtable (0x14B005630): lea rcx,[rip+..] at 0x147C27013 in the TransferManager ctor 0x147C26450 (the 0x18-byte inner dao: "
     "+8 allocator, +0x10 hub; held by the CachedTransferblockDaoImpl at cache+8 and stored at TM+0x2C30)"),
    ("cachedblock_is_blocked", 0x147D702CC, "none", 0, 0,
     "bool CachedTransferblockDaoImpl::IsBlocked(this, int pid) 0x147D702CC (vtable slot 1): linear search of vector A (this+0x10..0x18) = HasOffersBlocked"),
    ("cachedblock_block", 0x147D73DB8, "none", 0, 0,
     "void CachedTransferblockDaoImpl::Block(this, int pid) 0x147D73DB8 (vtable slot 4): inner->vf[4] (flag 0 into TM+0x2F50), erase from B, push into A"),
    ("cachedblock_unblock", 0x147D73F2C, "none", 0, 0,
     "void CachedTransferblockDaoImpl::Unblock(this, int pid) 0x147D73F2C (vtable slot 7): inner->vf[7] (erase the flag-0 entry), erase from A"),
    # ---- Squad role (PlayerStatusManager, type 87) ---------------------------------------------------------
    ("psm_vtable", 0x147BE47B8, "rip", 6, 0x14AFFB2A0,
     "PlayerStatusManager vtable (0x14AFFB2A0): lea rax,[rip+..] at +6 of its ctor 0x147BE47B8 (manager type 87, 0x38 bytes, hub slot +0xAF8; "
     "+0x10 user team id, +0x14 count, +0x18 vector of 52 x 8-byte entries {int pid, i8 role, u8 wasPromised, u8 renewalDismissed})"),
    ("psm_handle_event", 0x147BF30C0, "none", 0, 0,
     "void PlayerStatusManager::HandleEvent(this, int eventId, Event*) 0x147BF30C0 (vtable slot 1): 0x5F joined-team -> AddPlayer, 0x60 left-team -> remove, "
     "0x17 season reset -> rebuild roles (0x147BF5D40), 5 -> 0x147EA898C"),
    ("psm_add_player", 0x147BE7BB4, "none", 0, 0,
     "bool PlayerStatusManager::AddPlayer(this, int pid, uint8 role) 0x147BE7BB4: AddEntry(this+0x10,pid); role 0 or 0xFF = auto (computed by 0x147E9DED8, wasPromised 0) "
     "else role 1..5 stored with wasPromised 1; entry+6 = 0"),
    ("psm_get_entry", 0x147D8BADC, "none", 0, 0,
     "Entry* PlayerStatusData::Find(sub = PSM+0x10, int pid) 0x147D8BADC: scans entries 0..51 of the vector at sub+8, stops at pid -1; null when missing. role = (i8)entry[+4]"),
    ("psm_remove_entry", 0x147D90C80, "none", 0, 0,
     "bool PlayerStatusData::Remove(sub, int pid) 0x147D90C80: erase by index over [sub+4] used entries, memmove, count--, append an empty entry {-1,0xFF,0,0}"),
    ("psm_clear", 0x147D91228, "none", 0, 0,
     "void PlayerStatusData::Clear(sub) 0x147D91228: sub+0 = -1, sub+4 = 0, 52 entries reset to {-1,0xFF,0,0}"),
    ("psm_add_entry", 0x147D811D0, "none", 0, 0,
     "Entry* PlayerStatusData::AddEntry(sub, int pid) 0x147D811D0: if count < 52: entry = begin + count*8; entry.pid = pid; count++; returns entry (role bytes not touched)"),
    ("psm_compute_role", 0x147E9DED8, "none", 0, 0,
     "int8 ComputeSquadRole(HubRef*, int teamId, int pid) 0x147E9DED8: squad-rank based default role 1..5 (user team path 0x147E9E0BC, other 0x147E9E08C)"),
    # ---- Morale (PlayerMoraleManager, type 83) --------------------------------------------------------------
    ("pmm_vtable", 0x147D7DE08, "rip", 0xF, 0x14B0156A8,
     "PlayerMoraleManager vtable (0x14B0156A8): lea rax,[rip+..] at +0xF of its ctor 0x147D7DE08 (manager type 83, 0x5B8 bytes, hub slot +0xA78; "
     "+0x518 morale store: +0 team id, +0x10/+0x18 vector of 0x60-byte records, max 52)"),
    ("pmm_handle_event", 0x147D8D354, "none", 0, 0,
     "void PlayerMoraleManager::HandleEvent(this, int eventId, Event*) 0x147D8D354 (vtable slot 1): 0x5F joined user team -> find/create record + InitMorale, "
     "0x60 left -> erase record, 0x17 season init (0x147D90F18), 0x73 sets +0x554"),
    ("pmm_find_record", 0x147D8B5E8, "none", 0, 0,
     "MoraleRecord* MoraleStore::Find(store = PMM+0x518, int pid) 0x147D8B5E8: linear scan of 0x60-byte records between store+0x10 and store+0x18, record+0 = pid; null if none"),
    ("pmm_create_record", 0x147D81108, "none", 0, 0,
     "MoraleRecord* MoraleStore::Create(store = PMM+0x518, int pid, int emotionMinus1) 0x147D81108: refuses at 52 records; default-constructs, +0 = pid, +4 = value, push_back; returns the record"),
    ("pmm_init_morale", 0x147D93B08, "none", 0, 0,
     "void PlayerMoraleManager::InitMorale(this, MoraleRecord*, PrevData* = null) 0x147D93B08: fills the components (+8..+0x38) and tail-calls SetTotal(this, rec, total)"),
    ("pmm_set_total", 0x147D960A8, "none", 0, 0,
     "void PlayerMoraleManager::SetTotalMorale(this, MoraleRecord*, int total) 0x147D960A8: rec+0x2C = total; when the morale level bucket changes tells the "
     "DynamicOverallManager (hub +0x1298) via 0x147B9C218(dyn, pid)"),
    ("pmm_get_morale", 0x147D8B528, "none", 0, 0,
     "int PlayerMoraleManager::GetMorale(this, int teamId, int pid, ctx) 0x147D8B528: record of the user team -> rec+0x2C, missing record -> 0"),
    # ---- DataController (hub slot 32): team links, emotion (grudge / love) ---------------------------------------
    ("dc_write_team_player_link", 0x147BA08CC, "none", 0, 0,
     "void DataController::WriteTeamPlayersLinks(this, int pid, int oldTeam, int newTeam, int jersey[stack]) 0x147BA08CC: UPDATE teamplayerlinks SET teamid=new, "
     "jerseynumber, position=29 WHERE teamid=old AND playerid=pid; posts PlayerLeftTeam (0x60: +0x18 pid, +0x1C old, +0x20 new) then PlayerJoinedTeam (0x5F: +0x18 pid, +0x1C new, +0x20 old)"),
    ("dc_get_emotion", 0x147B8868C, "none", 0, 0,
     "int DataController::GetEmotion(this, int teamId, int pid) 0x147B8868C: SELECT level_of_emotion FROM player_grudgelove WHERE playerid AND emotional_teamid; 0 when no row"),
    ("dc_set_grudge", 0x147BA02D8, "none", 0, 0,
     "bool DataController::SetEmotionLevel1(this, int teamId, int pid) 0x147BA02D8: insert / update the player_grudgelove row (team, pid) with level_of_emotion = 1 (max 7 rows per player, 0x147B5F52C prunes)"),
    ("dc_player_emotion", 0x147B866BC, "none", 0, 0,
     "int DataController::GetPlayerEmotionType(this, int pid) 0x147B866BC: SELECT emotion FROM players WHERE playerid (1..8); morale record +4 = this - 1"),
    ("tm_clear_dao_caches", 0x147C59538, "none", 0, 0,
     "void TransferManager::ClearDaoCaches(tm) 0x147C59538: re-fills the cached DAOs from their stores; the cached transferblock dao (TM+0x2D38) clears "
     "vectors A/B and refills them from TM+0x2F50 (flag 0 -> A, flag 1 -> B). Called from TransferManager::HandleEvent 0x147C42494 (twice) and 0x147C59FEC"),
    ("dc_get_player_traits", 0x147B75024, "none", 0, 0,
     "void DataController::GetPlayerTraits(this, int pid, uint8 out[]) 0x147B75024: SELECT trait1, trait2, icontrait1, icontrait2 FROM players; out[i] = 0/1/2 (1 = trait, 2 = icon trait), i = bit index (trait2 bit 10 = CAREER_ONE_CLUB_PLAYER = index 40)"),
    ("dc_set_player_traits", 0x147B9FB40, "none", 0, 0,
     "void DataController::SetPlayerTraits(this, int pid, uint8 in[]) 0x147B9FB40: UPDATE players SET trait1, trait2, icontrait1, icontrait2 from the byte array"),
    ("game_allocator_ptr", 0x147BA09B3, "rip", 0, 0x14C269EA8,
     "mov rcx,[rip+..] at 0x147BA09B3: the game's allocator pointer (global 0x14C269EA8; object -> vtable, slot 2 (+0x10) = alloc(this, size, const char* name, flags) returns the block); "
     "career events are allocated with it (size 0x28 for 0x5F/0x60, 0x20 for 0xBE) and freed by the event's Release"),
    # ---- event object vtables (for a native that posts the game's own join / leave / block events) --------------------
    ("ev_vtable_player_joined", 0x147BA0A33, "rip", 0, 0x14AFF6C00,
     "PlayerJoinedTeam event vtable (0x14AFF6C00): lea rcx,[rip+..] at 0x147BA0A33 in DataController::WriteTeamPlayersLinks (event id 0x5F, object 0x28 bytes)"),
    ("ev_vtable_player_left", 0x147BA09DF, "rip", 0, 0x14AFF6468,
     "PlayerLeftTeam event vtable (0x14AFF6468): lea rax,[rip+..] at 0x147BA09DF in DataController::WriteTeamPlayersLinks (event id 0x60, object 0x28 bytes)"),
    ("ev_vtable_transfer_blocked", 0x147F68A47, "rip", 0, 0x14AFF7570,
     "TransferBlockedByUserEvent vtable (0x14AFF7570): lea rdx,[rip+..] at 0x147F68A47 in ToggleTransferBlock (event id 0xBE, object 0x20 bytes)"),
]


def check_json():
    doc = json.load(open(JSON_PATH))
    bad = 0
    for name, e in doc["signatures"].items():
        hits = find(e["pattern"])
        want = int(e["va"], 16)
        ok = len(hits) == 1 and hits[0] == want
        tgt = None
        if e["resolve"] == "rip":
            tgt = rx.rip_target(want + e["offset"])
            ok = ok and tgt == int(e["target"], 16)
        print("%-28s hits=%d %s%s" % (name, len(hits), "OK" if ok else "MISMATCH",
                                      ("  rip -> %x" % tgt) if tgt else ""))
        if not ok:
            bad += 1
    print("ALL UNIQUE AND RESOLVED" if not bad else "PROBLEMS: %d" % bad)
    return bad


def main():
    if "--check" in sys.argv:
        sys.exit(1 if check_json() else 0)
    out = {"build": BUILD, "game": "FC27.exe", "signatures": {}}
    ok = True
    for name, va, resolve, off, target, note in ANCHORS:
        sig, n = make_sig(va, 24, 140)
        if n != 1:
            print("!! %s: %d matches" % (name, n))
            ok = False
        if resolve == "rip":
            t = rx.rip_target(va + off)
            if t != target:
                print("!! %s: rip at %x resolves to %s, expected %x" % (name, va + off, ("%x" % t) if t else "none", target))
                ok = False
        print("%-28s %x  matches=%d  len=%d" % (name, va, n, len(sig.split())))
        out["signatures"][name] = {"pattern": sig, "resolve": resolve, "offset": off, "note": note, "va": "0x%X" % va,
                                   "target": "0x%X" % (target or va)}
    if "--json" in sys.argv:
        with open(JSON_PATH, "w") as f:
            json.dump(out, f, indent=1)
        print("written", JSON_PATH)
    print("ALL UNIQUE" if ok else "PROBLEMS")


if __name__ == "__main__":
    main()
