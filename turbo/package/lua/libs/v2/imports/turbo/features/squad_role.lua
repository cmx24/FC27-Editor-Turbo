-- Turbo feature: set the squad role of your club's players (Tools > Your squad, Teams > Mass actions > Squad roles, the
-- Players editor's Squad role, the match crash guard of bridge.lua).
-- Port of FC 26 mass_edit_squadrole.lua. In FC 27 Live Editor, SetSquadRole is a "TODO: FC27" stub.
-- FC 27 keeps the role in the career's PlayerStatusManager: one entry per player of your club, at most 52
-- (docs/re/player_status_roles.md sections 3 and 7.2). Turbo writes it
--   1) through the game itself when Turbo.dll has the squad_role call (TurboSquadRole, op 14, turbogui/src/core/squad_role.h):
--      an existing entry gets its role byte, a player without one (created players, Turbo's database moves before 1.2.0:
--      no PlayerJoinedTeam event, so the game never added one) gets one from the game's own AddPlayer; when the table is full,
--      entries of players who left the club (not lent out) go first through the game's Remove;
--   2) else in memory, the same steps done by Turbo (M.repair_entries, then the role byte; the FC 26 search heuristic
--      M.locate for a table that is not the FC 27 layout), and career_playercontract.playerrole when that table exists (FC 26).
-- Loaned-in players are included: the game gives them an entry when they join (its 0x5F handler has no loan test), the Squad
-- screen shows their role and the match load needs the entry. "include_loaned_in": false leaves their role alone.
-- The Squad screen reads the roles when it is built: open it again to see them. The game recomputes every role at the season
-- reset (event 0x17): auto.squad_role (opt-in) puts them back then.
-- Roles: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Prospect
--   "squad_role": { "role": 3, "include_loaned_in": true, "use_memory": true }   (include_loaned_in false: loaned-in players left alone)
--   one player: { "playerid": 1234, "role": 2 }; read his role: { "playerid": 1234, "mode": "check" }
--   auto.squad_role: { "enabled": false, "rule": "age" | "role", "rule_role": 3, "events": ["SEASON_RESET"] }
--
-- Squad-role fix (docs/TURBO_2_0_PLAN.md section 2):
--   * the players to write are your club's players (club_players: teamplayerlinks plus the team sheet); cm_teamsheets is only
--     the anchor locate() scores the vector against (fix 1), psm_members lists the role list's own entries,
--   * locate() is tolerant: role bytes 0..5 / 0xFF, and 90% valid entries are enough (fix 3),
--   * the summary counts every player that was not written and why (fix 4),
--   * the game rebuilds all roles on a season reset: the saved rule (turbo_output\role_rule.json, written by team_mass.lua)
--     is applied again after event 23 = 0x17 = SEASON_RESET (fix 5, M.on_event), unless turbo_output\role_reapply_off.txt exists.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local calib = require 'imports/turbo/core/calib'
local log = require 'imports/turbo/core/log'

local M = {}

M.FC26_LAYOUT = { offset = 0x18, size = 8, role_off = 4 }
M.SEASON_RESET = 23              -- 0x17: the game rebuilds the role list (docs/re/player_status_roles.md section 3.1)
M.REAPPLY_KILL_FILE = "role_reapply_off.txt"
M.REAPPLY_TRIES = 4              -- career events after SEASON_RESET on which a failed re-apply is tried again
M.VALID_FRACTION = 0.9           -- share of squad entries whose role byte must look like a role
local ENTRY_SIZES = { 8, 12, 16 }
local MAX_ENTRIES = 400
local LOANED_IN = { [1] = true, [3] = true, [5] = true }
M.ROLE_NAMES = { "Crucial", "Important", "Rotation", "Sporadic", "Prospect" }
M.SCREEN_NOTE = "open the Squad screen again to see them (it reads the roles when it is built)"

function M.role_name(r)
    if r == 0xFF then return "none" end
    return M.ROLE_NAMES[r] or tostring(r)
end

-- "Name (id)" for the result texts (the id alone when Live Editor has no name for him)
function M.who(pid)
    local name = game.player_name(pid)
    if name == tostring(pid) then return tostring(pid) end
    return string.format("%s (%d)", name, pid)
end

-- Turbo.dll's squad_role call (bridge.lua M.squad_role) when installed, else nil
function M.native()
    local f = _G["TurboSquadRole"]
    return type(f) == "function" and f or nil
end

local function role_byte_ok(role)
    return role ~= nil and ((role >= 0 and role <= 5) or role == 0xFF)
end

-- matched squad entries, entries whose role byte looks like a role, list of {pid, byte} for those that do not
local function score_vector(base, count, size, role_off, squad)
    local matched, valid_roles, bad = 0, 0, {}
    for i = 0, count - 1 do
        local e = base + i * size
        local pid = mem.int(e)
        if pid and squad[pid] then
            matched = matched + 1
            -- FC 27 stores the role in ONE byte ({int pid, int8 role, u8 wasPromised, u8 renewalDismissed}): read the byte, a
            -- promised player's int would be 0x1xx (docs/re/player_status_roles.md section 3). 0xFF = no role yet.
            local role = mem.byte(e + role_off)
            if role_byte_ok(role) then
                valid_roles = valid_roles + 1
            elseif #bad < 8 then
                bad[#bad + 1] = { pid = pid, byte = role }
            end
        end
    end
    return matched, valid_roles, bad
end

-- Find the role vector. Returns layout {mgr, offset, size, role_off, begin, count, matched, bad} or nil, reason
-- Entries with no player (pid <= 0: the list may keep empty slots) are not counted as matches, so a vector of 52 entries
-- of which only the squad's are filled still locates. One stray role byte (a value above 5) no longer rejects the vector:
-- at least VALID_FRACTION of the matched entries must look like roles, the others are logged with their player id.
function M.locate(squad, squad_count, hint)
    if type(ENUM_FCEGameModesFCECareerModePlayerStatusManager) ~= "number" then
        pcall(require, 'imports/career_mode/enums')
    end
    local mgr = mem.manager(ENUM_FCEGameModesFCECareerModePlayerStatusManager)
    if not mgr then return nil, "PlayerStatusManager not found" end
    if squad_count < 11 then return nil, "squad too small to anchor the search" end
    local need = math.max(11, math.floor(squad_count * 0.6))

    local tries = {}
    if type(hint) == "table" and util.to_int(hint.offset) and util.to_int(hint.size) then
        local size, role_off = util.to_int(hint.size), util.to_int(hint.role_off) or 4
        -- the role byte lies after the 4-byte player id and inside the entry: a saved layout that says otherwise (an
        -- edited or damaged calibration file) would read / write the next entry's player id, so it is not tried
        if role_off >= 4 and role_off < size then tries[#tries + 1] = { util.to_int(hint.offset), size, role_off } end
    end
    tries[#tries + 1] = { M.FC26_LAYOUT.offset, M.FC26_LAYOUT.size, M.FC26_LAYOUT.role_off }
    for off = 0x08, 0x200, 8 do
        for _, size in ipairs(ENTRY_SIZES) do tries[#tries + 1] = { off, size, 4 } end
    end

    local best = nil
    for _, t in ipairs(tries) do
        local b, _, count = mem.vector(mgr + t[1], t[2], MAX_ENTRIES)
        if b and count and count >= 11 then
            local matched, valid, bad = score_vector(b, count, t[2], t[3], squad)
            local density = matched / count
            -- Prefer more squad matches; on a tie prefer the entry size where squad players fill the
            -- vector densely (an 8-byte stride over 16-byte entries also "matches", at half density).
            if matched >= need and valid >= matched * M.VALID_FRACTION and (best == nil or matched > best.matched
                or (matched == best.matched and density > best.density)) then
                best = { mgr = mgr, offset = t[1], size = t[2], role_off = t[3], begin = b, count = count,
                    matched = matched, density = density, bad = bad, valid = valid }
                if t == tries[1] and matched >= squad_count * 0.9 and density >= 0.9 and #bad == 0 then break end
            end
        end
    end
    if not best then return nil, "no role list matching your squad was found in PlayerStatusManager" end
    if #best.bad > 0 then
        local parts = {}
        for _, x in ipairs(best.bad) do parts[#parts + 1] = string.format("player %d = %s", x.pid, tostring(x.byte)) end
        log.warn("squad_role: the role list at +0x%X has %d of %d squad entries with a role byte outside 0..5 / 0xFF (%s); used anyway",
            best.offset, best.matched - best.valid, best.matched, table.concat(parts, ", "))
    end
    return best
end

-- Every player of the role list: set {[pid]=true}, count, list of {pid, addr (role byte), role (byte), index}
-- (entries with no player, pid <= 0, are left out)
function M.psm_members(layout)
    local set, list, n = {}, {}, 0
    if type(layout) ~= "table" then return set, 0, list end
    for i = 0, layout.count - 1 do
        local e = layout.begin + i * layout.size
        local pid = mem.int(e)
        if pid and pid > 0 and not set[pid] then
            set[pid] = true
            n = n + 1
            list[#list + 1] = { pid = pid, addr = e + layout.role_off, role = mem.byte(e + layout.role_off), index = i }
        end
    end
    return set, n, list
end

-- Players on the user's team in teamplayerlinks (club links), or nil when the table is missing
local function team_links(teamid)
    local links = db.get_table("teamplayerlinks")
    if not links or not db.has_fields(links, { "teamid", "playerid" }) then return nil end
    local out = {}
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then
            local pid = links:GetRecordFieldValue(rec, "playerid")
            if pid and pid > 0 then out[pid] = true end
        end
    end
    return out
end

-- Players in the squad who are on loan from another club (playerloans table, when present)
local function loaned_in(squad, user_team)
    local out = {}
    local loans = db.get_table("playerloans")
    if not loans or not db.has_fields(loans, { "playerid", "teamidloanedfrom" }) then return out end
    for rec in db.records(loans) do
        local pid = loans:GetRecordFieldValue(rec, "playerid")
        local from = loans:GetRecordFieldValue(rec, "teamidloanedfrom")
        if squad[pid] and from ~= user_team then out[pid] = true end
    end
    return out
end

-- ---------------------------------------------------------------- missing entries (match crash guard)
-- FC 27's PlayerStatusManager keeps one entry per player of your club (docs/re/player_status_roles.md section 3): +0x10
-- int32 team, +0x14 int32 used count, +0x18 begin of 52 fixed slots of {int32 pid, int8 role, u8 wasPromised,
-- u8 renewalDismissed} (+0x20 / +0x28 = begin + 0x1A0), used entries first, empties {-1, 0xFF, 0, 0}. The game adds an
-- entry when a player joins (event 0x5F); players Turbo linked to the club through the database (created players, the
-- database moves before 1.2.0) never got one. The game's match code reads Find(pid)->role without a null check
-- (FC27.exe+0x7FCCDF0, seen in a crash dump 2026-10-05 22:50: the match load died on such a player), and the mass
-- action had no entry to write for them. repair_entries adds the missing entries the way the game's AddEntry +
-- AddPlayer do (section 7.2, memory path): the entry first, then the count.
M.PSM = { team = 0x10, count = 0x14, begin = 0x18, finish = 0x20, cap = 0x28, slots = 52, size = 8 }

-- The validated table: { mgr, begin, count, pids = {[pid] = index} } or nil, reason. Nothing is trusted that the
-- section 7.2 checks do not confirm.
function M.psm_state(user_team)
    if type(ENUM_FCEGameModesFCECareerModePlayerStatusManager) ~= "number" then
        pcall(require, 'imports/career_mode/enums')
    end
    local mgr = mem.manager(ENUM_FCEGameModesFCECareerModePlayerStatusManager)
    if not mgr then return nil, "PlayerStatusManager not found" end
    local P = M.PSM
    local team, count = mem.int(mgr + P.team), mem.int(mgr + P.count)
    if not team or team <= 0 or team ~= user_team then
        return nil, string.format("PlayerStatusManager is not set up for your club yet (team %s, your club %s)", tostring(team), tostring(user_team))
    end
    if not count or count < 0 or count > P.slots then return nil, "PlayerStatusManager count " .. tostring(count) .. " is out of range" end
    local b, fin, cap = mem.ptr(mgr + P.begin), mem.ptr(mgr + P.finish), mem.ptr(mgr + P.cap)
    local span = P.slots * P.size
    if not b or fin ~= b + span or cap ~= b + span or not mem.readable(b, span) then
        return nil, "PlayerStatusManager entries do not have the FC 27 layout (52 slots of 8 bytes)"
    end
    local pids = {}
    for i = 0, P.slots - 1 do
        local e = b + i * P.size
        local pid, role = mem.int(e), mem.byte(e + 4)
        if i < count then
            if not pid or pid <= 0 or pids[pid] then return nil, string.format("PlayerStatusManager entry %d is not a player (%s)", i, tostring(pid)) end
            if not role or not ((role >= 1 and role <= 5) or role == 0xFF) then
                return nil, string.format("PlayerStatusManager entry %d has role %s", i, tostring(role))
            end
            pids[pid] = i
        elseif pid ~= -1 then
            return nil, string.format("PlayerStatusManager slot %d after the used ones is not empty", i)
        end
    end
    return { mgr = mgr, begin = b, count = count, pids = pids }
end

-- Every player linked to your club in teamplayerlinks (loaned-in players too: the game gives them an entry when they
-- join), plus the team sheet's players. Returns {[pid] = true}, count
function M.club_players(user_team)
    local out, n = {}, 0
    local links = db.get_table("teamplayerlinks")
    if links and db.has_fields(links, { "teamid", "playerid" }) then
        for rec in db.records(links) do
            if links:GetRecordFieldValue(rec, "teamid") == user_team then
                local pid = links:GetRecordFieldValue(rec, "playerid")
                if pid and pid > 0 and not out[pid] then out[pid] = true; n = n + 1 end
            end
        end
    end
    local squad = game.user_squad()
    for pid in pairs(squad) do
        if not out[pid] then out[pid] = true; n = n + 1 end
    end
    return out, n
end

-- Players your club has lent out (playerloans.teamidloanedfrom = your club): {[pid] = true}; their entries are kept
function M.loaned_out(user_team)
    local out = {}
    local loans = db.get_table("playerloans")
    if not loans or not db.has_fields(loans, { "playerid", "teamidloanedfrom" }) then return out end
    for rec in db.records(loans) do
        if loans:GetRecordFieldValue(rec, "teamidloanedfrom") == user_team then out[loans:GetRecordFieldValue(rec, "playerid")] = true end
    end
    return out
end

-- With Turbo.dll's squad_role call: the stale entries go through the game's Remove (code 4: Turbo.dll refuses a player the game
-- still sees at your club), the missing ones come from its AddPlayer (code 1 with the role, code 2 with the game's own role).
-- Returns added, text, left, created; or nil, nil, nil, nil, why when the call is off before anything was done (the memory
-- path takes over).
local function repair_native(native, st, club, missing, role_of, prefer_game, user_team)
    local P = M.PSM
    local removed, notes = 0, {}
    local need = st.count + #missing - P.slots
    if need > 0 then
        local lent = M.loaned_out(user_team)
        -- the candidates from the table as read (the game's Remove moves the entries after it down: by pid, not by slot)
        local victims = {}
        for i = 0, st.count - 1 do
            local pid = mem.int(st.begin + i * P.size)
            if pid and not club[pid] and not lent[pid] then victims[#victims + 1] = pid end
        end
        for _, pid in ipairs(victims) do
            if removed >= need then break end
            local ok, text, status, out0 = native(4, pid, 0)
            if status == "off" and removed == 0 then return nil, nil, nil, nil, text end
            if ok and status == "ok" and out0 == 1 then
                removed = removed + 1
            else
                notes[#notes + 1] = tostring(text)   -- the game still sees him at your club, or the call is queued / off
                if status == "queued" or status == "off" then break end
            end
        end
    end
    local added, had, names, created, fails = 0, 0, {}, {}, {}
    for _, pid in ipairs(missing) do
        local role = role_of and role_of(pid) or nil
        if math.type(role) ~= "integer" or role < 1 or role > 5 then role = nil end
        local code = (role and not prefer_game) and 1 or 2
        local ok, text, status, out0, how = native(code, pid, role or 0)
        if status == "off" and added == 0 and removed == 0 then return nil, nil, nil, nil, text end
        if ok and status == "ok" and how == 1 then
            added = added + 1
            created[pid] = { role = out0, via = "game" }
            names[#names + 1] = string.format("%d (role %s%s)", pid, tostring(out0), code == 2 and ", the game's choice" or "")
        elseif ok and status == "ok" and how == 2 then
            had = had + 1   -- he got one in the meantime (the game's own event)
        else
            fails[#fails + 1] = string.format("%d: %s", pid, tostring(text))
            if status == "queued" or status == "off" then break end
        end
    end
    local left = #missing - added - had
    local text = string.format("squad status entries added for %d of %d players without one through the game's AddPlayer: %s", added, #missing,
        table.concat(names, ", "))
    if removed > 0 then
        text = text .. string.format(" (%d entries of players no longer at your club removed to make room, the game's Remove)", removed)
    end
    if left > 0 then
        text = text .. string.format("; %d left out (%s)", left, fails[1] or notes[1] or "the game's table is full")
    end
    return added, text, left, created
end

-- Adds an entry for each player of your club who has none. role_of(pid) -> 1..5 (nil or out of range: the game's own role
-- with Turbo.dll's call, Rotation 3 in memory). opts.native: the TurboSquadRole function (default M.native(); false = memory
-- only); opts.prefer_game_role: with the game's call every missing player gets the game's own role (as a signing without a
-- contract role). Returns added (number), summary, missing_left (number), created ({[pid] = {role, via = "game" | "memory"}});
-- added = nil with the reason when nothing could be checked.
function M.repair_entries(role_of, dry, opts)
    opts = opts or {}
    if not mem.map_available() then return nil, mem.NO_MAP end
    local user_team = game.user_team_id()
    if not user_team or user_team <= 0 then return nil, "user team not found" end
    local st, err = M.psm_state(user_team)
    if not st then return nil, err end
    local club, n = M.club_players(user_team)
    local function missing_of(state)
        local out = {}
        for pid in pairs(club) do
            if not state.pids[pid] then out[#out + 1] = pid end
        end
        table.sort(out)
        return out
    end
    local missing = missing_of(st)
    if #missing == 0 then return 0, string.format("all %d players of your club have a squad status entry", n), 0, {} end
    local native = opts.native
    if native == nil then native = M.native() end
    local note = ""
    if native and not dry then
        local added, text, left, created, off = repair_native(native, st, club, missing, role_of, opts.prefer_game_role == true, user_team)
        if added then return added, text, left, created end
        note = string.format(" (in memory: %s)", tostring(off))
    end
    local P = M.PSM
    -- no room: entries of players who are no longer at your club (and not out on loan from it) go first, the way the
    -- game's Remove (0x147D90C80) does it: the entries after it move down one slot, an empty one goes last, count - 1
    local removed = 0
    if st.count + #missing > P.slots then
        local lent = M.loaned_out(user_team)
        local i = 0
        while i < st.count and st.count + #missing > P.slots do
            local e = st.begin + i * P.size
            local pid = mem.int(e)
            if pid and not club[pid] and not lent[pid] then
                if not dry then
                    for j = i, st.count - 2 do
                        MEMORY:WriteBytes(st.begin + j * P.size, MEMORY:ReadBytes(st.begin + (j + 1) * P.size, P.size))
                    end
                    local last = st.begin + (st.count - 1) * P.size
                    MEMORY:WriteInt(last, -1)
                    MEMORY:WriteBytes(last + 4, { 0xFF, 0, 0 })
                    MEMORY:WriteInt(st.mgr + P.count, st.count - 1)
                end
                st.count = st.count - 1
                removed = removed + 1
            else
                i = i + 1
            end
        end
    end
    local added, names, created = 0, {}, {}
    for _, pid in ipairs(missing) do
        if st.count >= P.slots then break end
        local role = role_of and role_of(pid) or nil
        if math.type(role) ~= "integer" or role < 1 or role > 5 then role = 3 end
        local e = st.begin + st.count * P.size
        if not dry then
            MEMORY:WriteInt(e, pid)
            MEMORY:WriteBytes(e + 4, { role, 0, 0 })
            if mem.int(e) ~= pid then return added, string.format("squad status entry for player %d did not read back", pid), #missing - added, created end
            MEMORY:WriteInt(st.mgr + P.count, st.count + 1)
        end
        st.count = st.count + 1
        st.pids[pid] = st.count - 1
        added = added + 1
        created[pid] = { role = role, via = "memory" }
        names[#names + 1] = string.format("%d (role %d)", pid, role)
    end
    local left = #missing - added
    local text = string.format("squad status entries added for %d of %d players without one%s: %s", added, #missing, note, table.concat(names, ", "))
    if removed > 0 then text = text .. string.format(" (%d entries of players no longer at your club removed to make room)", removed) end
    if left > 0 then
        text = text .. string.format("; %d left out: the game's table is full (%d slots, entries of players who left stay until the game removes them)", left, P.slots)
    end
    return added, (dry and "[DRY RUN] " or "") .. text, left, created
end

-- ---------------------------------------------------------------- roles
-- The role byte of one player in memory (FC 27 table, else the FC 26 search). Returns
--   write(pid, role) -> what, role | nil, reason   (nil, reason instead of write when no table was found),
--   label, read(pid) -> role byte | nil, members {[pid] = index} (every entry with a player: the role list's own players),
--   layout (the FC 26 search's; nil for the FC 27 table).
-- read / members serve the report (team_mass's rule preview, the 2.0 counts): no byte is written until write() is called.
local function memory_writer(ctx, club, user_team)
    local st = M.psm_state(user_team)
    if st then
        local function addr(pid)
            local i = st.pids[pid]
            return i and (st.begin + i * M.PSM.size + 4) or nil
        end
        return function(pid, role)
            local a = addr(pid)
            if not a then return nil, "no squad status entry (the game's table is full)" end
            local before = mem.byte(a)
            if before == role then return "unchanged", role end
            if not ctx.dry then MEMORY:WriteBytes(a, { role }) end   -- one byte: the two flags after it stay
            return "updated", role
        end, "in memory", function(pid) local a = addr(pid); return a and mem.byte(a) or nil end, st.pids, nil
    end
    local count = util.count(club)
    local layout, lerr = M.locate(club, count, calib.get(ctx.out_dir, "squad_role"))
    if not layout then return nil, lerr end
    if not ctx.dry then
        calib.put(ctx.out_dir, "squad_role", { offset = layout.offset, size = layout.size, role_off = layout.role_off })
    end
    local index = {}
    for i = 0, layout.count - 1 do
        local pid = mem.int(layout.begin + i * layout.size)
        if pid and pid > 0 and index[pid] == nil then index[pid] = i end
    end
    local function addr(pid)
        local i = index[pid]
        return i and (layout.begin + i * layout.size + layout.role_off) or nil
    end
    return function(pid, role)
        local a = addr(pid)
        if not a then return nil, "not in the role list" end
        if mem.byte(a) == role then return "unchanged", role end
        if not ctx.dry then MEMORY:WriteBytes(a, { role }) end
        return "updated", role
    end, string.format("in memory (PlayerStatusManager +0x%X, entry %d bytes)", layout.offset, layout.size),
        function(pid) local a = addr(pid); return a and mem.byte(a) or nil end, index, layout
end

local OUTCOMES = {
    { key = "updated", label = "updated" }, { key = "created", label = "entry created" }, { key = "unchanged", label = "unchanged" },
    { key = "planned", label = "to set" }, { key = "skipped", label = "skipped" }, { key = "failed", label = "not done" },
}

-- The per-player lines: one per outcome, "Name (id) Role" (skipped / not done: with the reason)
local function details_of(rows)
    local lines = {}
    for _, o in ipairs(OUTCOMES) do
        local list = {}
        for _, r in ipairs(rows) do
            if r.what == o.key then
                if r.why then
                    list[#list + 1] = string.format("%s: %s", M.who(r.pid), r.why)
                else
                    list[#list + 1] = string.format("%s %s", M.who(r.pid), M.role_name(r.role))
                end
            end
        end
        if #list > 0 then lines[#lines + 1] = string.format("  %s (%d): %s", o.label, #list, table.concat(list, ", ")) end
    end
    return table.concat(lines, "\n")
end

-- Sets roles for your club's players: role_of(pid) -> 1..5, or nil (, why) to leave him alone. `label` starts the summary.
-- opts.players: {[pid] = true} to set (default: every player of your club). Turbo's team mass actions use it with a role by
-- rule, the Players editor with one player. ctx.preview = true (with ctx.dry): nothing is written, the summary says so.
-- Returns ok, summary (one line), details (a line per outcome: each player), report (the per-player facts for team_mass's
-- rule preview: {plan = {[pid]=role}, old = {[pid]=role byte before}, reasons = {[why]=count}, skipped = {[pid]=why},
-- wrote = n, loaned = n, no_entry = {pids planned but with no entry in the role list}, outside = {role list pids not at your
-- club, left alone}, layout = the FC 26 search's layout when that path was used}).
function M.apply(ctx, role_of, label, opts)
    opts = opts or {}
    local user_team = game.user_team_id()
    if not user_team or user_team <= 0 then return false, "user team not found" end
    local club, n = M.club_players(user_team)
    if n == 0 then return false, string.format("no players found for your club (team %d)", user_team) end
    local skip_loans = (ctx.cfg.include_loaned_in == false) and loaned_in(club, user_team) or {}

    local ids, rows, plan = {}, {}, {}
    for pid in pairs(opts.players or club) do ids[#ids + 1] = pid end
    table.sort(ids)
    for _, pid in ipairs(ids) do
        local row = { pid = pid }
        rows[#rows + 1] = row
        if not club[pid] then
            row.what, row.why = "skipped", "not in your club"
        elseif skip_loans[pid] then
            row.what, row.why = "skipped", "loaned in (include_loaned_in is off)"
        else
            local r, why = role_of(pid)
            if math.type(r) == "integer" and r >= 1 and r <= 5 then
                plan[pid], row.role = r, r
            else
                row.what, row.why = "skipped", why or "no role for him (age not known?)"
            end
        end
    end
    local parts, contract_rows = {}, 0
    local report = { plan = plan, old = {}, reasons = {}, skipped = {}, outside = {}, no_entry = {}, wrote = 0, loaned = 0 }

    -- 1) career_playercontract (FC 26; FC 27's database has no such table)
    local contracts = db.get_table("career_playercontract")
    if contracts and db.has_fields(contracts, { "playerid", "playerrole", "contract_status" }) then
        local c = 0
        for rec in db.records(contracts) do
            local pid = contracts:GetRecordFieldValue(rec, "playerid")
            local role = plan[pid]
            if role and not LOANED_IN[contracts:GetRecordFieldValue(rec, "contract_status")] then
                if report.old[pid] == nil then report.old[pid] = contracts:GetRecordFieldValue(rec, "playerrole") end
                local v, verr = db.validate(contracts, "playerrole", role)
                if v == nil then return false, verr end
                local wok, werr = db.set(contracts, rec, "playerrole", role, ctx.dry)
                if not wok then return false, werr end
                c = c + 1
            end
        end
        parts[#parts + 1] = string.format("career_playercontract: %d rows", c)
        contract_rows = c
    end

    -- 2) the PlayerStatusManager: missing entries first (with the role asked for; players left alone: the game's own role),
    -- then the role of every planned player, through the game when Turbo.dll has the call
    local via = nil
    if ctx.cfg.use_memory == false then
        parts[#parts + 1] = "PlayerStatusManager: not touched (use_memory = false)"
        for _, r in ipairs(rows) do if not r.what then r.what, r.why = "skipped", "use_memory = false (memory not touched)" end end
    elseif not mem.map_available() then
        parts[#parts + 1] = "PlayerStatusManager: " .. mem.NO_MAP
        for _, r in ipairs(rows) do if not r.what then r.what, r.why = "failed", "the Turbo GUI is not running" end end
    else
        local native = M.native()
        local added, rtext, left, created = M.repair_entries(function(pid) return plan[pid] end, ctx.dry, { native = native or false })
        created = created or {}
        -- the entries created for the players set are in the per-player lines; the repair's own text only when it says more
        -- (stale entries removed, players left out, entries for players this run leaves alone)
        local more = added and ((left or 0) > 0 or tostring(rtext):find("removed to make room", 1, true) ~= nil)
        for pid in pairs(created) do if not plan[pid] then more = true end end
        if more then parts[#parts + 1] = rtext end
        if added == nil then log.warn("squad_role: missing entries not checked: %s", tostring(rtext)) end
        -- the role table in memory: the old roles and the list's own entries for the report, the writer when the game call is
        -- not there (nothing is written before a row needs it)
        local write, wlabel, read, members, layout = memory_writer(ctx, club, user_team)
        local werr, off_note = nil, nil
        if layout then report.layout = layout end
        if read then
            for pid in pairs(plan) do
                if not created[pid] then report.old[pid] = read(pid) end
            end
        end
        local queued = false
        for _, r in ipairs(rows) do
            if not r.what then
                local c = created[r.pid]
                if c and c.role == r.role then
                    r.what = "created"
                    if not ctx.dry then via = via or (c.via == "game" and "through the game (Turbo.dll squad_role)" or "in memory") end
                elseif ctx.dry then
                    r.what = "planned"
                elseif queued then
                    r.what, r.why = "failed", "a game call is still queued: run it again"
                else
                    if native then
                        local ok, text, status, out0, how = native(1, r.pid, r.role)
                        if status == "off" then
                            native, off_note = nil, tostring(text)
                        elseif ok and status == "ok" then
                            via = via or "through the game (Turbo.dll squad_role)"
                            r.what = (how == 1 and "created") or (how == 2 and "unchanged") or "updated"
                            r.role = out0 or r.role
                        elseif status == "queued" then
                            queued, r.what, r.why = true, "failed", "queued for the game thread: run it again after the next career-mode event"
                        else
                            r.what, r.why = "failed", tostring(text)
                        end
                    end
                    if not r.what then
                        if not write then
                            werr = werr or wlabel or "role table not found"
                            r.what, r.why = "failed", tostring(werr)
                        elseif layout and not members[r.pid] then
                            -- the FC 26 search found the list but he has no entry in it: Turbo adds entries to the FC 27 table
                            -- only (M.repair_entries); the game computes his role when the Squad screen opens
                            r.what, r.why = "skipped", "no entry in the role list"
                        else
                            via = via or wlabel
                            local what, role = write(r.pid, r.role)
                            if what then r.what, r.role = what, role else r.what, r.why = "failed", tostring(role) end
                        end
                    end
                end
            end
        end
        if off_note then parts[#parts + 1] = "the game call is off, in memory instead: " .. off_note end
        if werr then
            parts[#parts + 1] = "PlayerStatusManager: " .. tostring(werr)
            log.warn("squad_role memory pass skipped: %s", tostring(werr))
        end
        -- the counts of the 2.0 role fix: planned players with no entry in the role list (none could be made), entries of
        -- players who are not at your club (left alone)
        if members then
            for _, r in ipairs(rows) do
                if plan[r.pid] and not members[r.pid] and (r.what == "planned" or r.what == "skipped" or r.what == "failed") then
                    report.no_entry[#report.no_entry + 1] = r.pid
                end
            end
            for pid in pairs(members) do
                if not club[pid] then report.outside[#report.outside + 1] = pid end
            end
            table.sort(report.outside)
            if #report.no_entry > 0 then
                parts[#parts + 1] = string.format("%d squad players have no entry in the role list", #report.no_entry)
                log.warn("squad_role: no role list entry for players %s", table.concat(report.no_entry, ","))
            end
            if #report.outside > 0 then
                parts[#parts + 1] = string.format("%d role list players are not in your squad (left alone)", #report.outside)
                log.info("squad_role: role list players outside your club (left alone): %s", table.concat(report.outside, ","))
            end
        end
    end

    local count = {}
    for _, r in ipairs(rows) do count[r.what] = (count[r.what] or 0) + 1 end
    local set_n = (count.updated or 0) + (count.created or 0) + (count.unchanged or 0)
    local summary
    if ctx.dry then
        summary = string.format("%s: %d of %d players would get the role%s", label, (count.planned or 0) + (count.created or 0), #rows,
            (count.created or 0) > 0 and string.format(" (%d of them need an entry first)", count.created) or "")
    else
        summary = string.format("%s: %d of %d players have the role now%s (%d updated, %d entries created, %d unchanged)", label, set_n, #rows,
            via and (" " .. via) or "", count.updated or 0, count.created or 0, count.unchanged or 0)
    end
    if (count.skipped or 0) > 0 then summary = summary .. string.format("; %d skipped", count.skipped) end
    if (count.failed or 0) > 0 then summary = summary .. string.format("; %d not done", count.failed) end
    if #parts > 0 then summary = summary .. "; " .. table.concat(parts, "; ") end
    if set_n > 0 and not ctx.dry then summary = summary .. "; " .. M.SCREEN_NOTE end
    local ok = (count.failed or 0) == 0 and (set_n > 0 or contract_rows > 0 or (ctx.dry and ((count.planned or 0) + (count.created or 0)) > 0))
    -- the per-player facts for the callers (team_mass's rule preview): plan, skipped with the reason, counts
    for _, r in ipairs(rows) do
        if r.what == "skipped" then
            report.skipped[r.pid] = r.why
            report.reasons[r.why] = (report.reasons[r.why] or 0) + 1
            if r.why and r.why:find("^loaned") then report.loaned = report.loaned + 1 end
        elseif r.what == "failed" then
            report.skipped[r.pid] = r.why
        end
    end
    report.wrote = count.updated or 0
    report.wrote = report.wrote + (count.created or 0)
    if ctx.preview == true then summary = "[PREVIEW, nothing written] " .. summary end
    return ok, summary, details_of(rows), report
end

-- summary + "\n" + details (the GUI shows the first line in its toast, all of it in the Mass actions panel); one_line: the details
-- after "; " instead (one player: the toast shows it all)
function M.join(summary, details, one_line)
    if type(details) ~= "string" or details == "" then return summary end
    if one_line then return summary .. "; " .. (details:gsub("^%s+", ""):gsub("\n%s*", "; ")) end
    return summary .. "\n" .. details
end

-- One player's role as the game holds it now: ok, text
function M.check(ctx, pid)
    if not pid or pid <= 0 then return false, "playerid is required" end
    local user_team = game.user_team_id()
    local native = M.native()
    if native then
        local ok, text, status, out0 = native(9, pid, 0)
        if ok and status == "ok" then
            if out0 == -2 then return true, string.format("%s has no squad status entry (the game computes a role when the Squad screen opens)", M.who(pid)) end
            return true, string.format("%s: squad role %s (read from the game)", M.who(pid), M.role_name(out0))
        end
        if status ~= "off" then return false, tostring(text) end
    end
    if not mem.map_available() then return false, mem.NO_MAP end
    local st, err = M.psm_state(user_team)
    if not st then return false, err end
    local i = st.pids[pid]
    if not i then return true, string.format("%s has no squad status entry", M.who(pid)) end
    return true, string.format("%s: squad role %s (read from memory)", M.who(pid), M.role_name(mem.byte(st.begin + i * M.PSM.size + 4)))
end

function M.run(ctx)
    local cfg = ctx.cfg
    local pid = util.to_int(cfg.playerid)
    if cfg.mode == "check" then return M.check(ctx, pid) end
    local role = util.to_int(cfg.role)
    if not role or role < 1 or role > 5 then return false, "role must be 1 (Crucial) .. 5 (Prospect)" end
    local opts = (pid and pid > 0) and { players = { [pid] = true } } or nil
    local ok, summary, details = M.apply(ctx, function() return role end, string.format("role %d (%s)", role, M.role_name(role)), opts)
    if ok and opts == nil and not ctx.dry then
        -- one catch-all rule (2.0), so the role also survives the game's next season reset
        local tm = require 'imports/turbo/features/team_mass'
        tm.save_role_rule(ctx.out_dir, { { name = "all", role = role } }, {})
    end
    return ok, M.join(summary, details, opts ~= nil)
end


-- auto.squad_role (opt-in): the game recomputes every role at the season reset (event 0x17, the PlayerStatusManager's own
-- rebuild); this puts Turbo's rule back right after it. rule "age": Rotation from team_mass.ADULT_AGE, Prospect below (the
-- Mass actions rule); rule "role": rule_role for everyone. Nothing runs when the auto feature is switched on (event -1): the
-- buttons apply the roles.
function M.auto(ctx, event_id)
    if event_id == nil or event_id == -1 then return end
    local cfg = ctx.cfg
    local role_of, label
    if cfg.rule == "role" then
        local r = util.to_int(cfg.rule_role)
        if not r or r < 1 or r > 5 then
            log.warn("squad_role auto: rule_role must be 1..5 (%s)", tostring(cfg.rule_role))
            return
        end
        role_of, label = function() return r end, string.format("role %d (%s)", r, M.role_name(r))
    else
        local tm = require 'imports/turbo/features/team_mass'
        local err
        role_of, err = tm.role_by_age(M.club_players(game.user_team_id()))
        if not role_of then
            log.warn("squad_role auto: %s", tostring(err))
            return
        end
        label = string.format("Rotation for age %d+, Prospect below", tm.ADULT_AGE)
    end
    local ok, summary, details = M.apply(ctx, role_of, label .. " (again after the season reset)")
    if ok then log.info("squad_role auto: %s", M.join(summary, details)) else log.warn("squad_role auto: %s", M.join(summary, details)) end
end

-- Re-apply after the game rebuilt the role list ---------------------------------------------------------------------------
-- The game recomputes every squad role on event 0x17 (= SEASON_RESET = 23) and when the user team changes. This tap runs
-- the saved rule again. UNCERTAIN (needs the in-game check, plan section 12): whether Live Editor's post__CareerModeEvent
-- handler is called for 0x17 at all, and whether it runs AFTER the game's own rebuild. So a failed attempt is retried on the next
-- REAPPLY_TRIES career events instead of being given up. Turbo.dll's bridge tap is not needed: this one is a plain
-- events.set_tap entry. Kill switch: turbo_output\role_reapply_off.txt.
function M.reapply_enabled(out_dir)
    if not out_dir then return false end
    return not util.file_exists(util.join(out_dir, M.REAPPLY_KILL_FILE))
end

local function reapply_now(event_id)
    local config = require 'imports/turbo/core/config'
    local env = require 'imports/turbo/core/env'
    local cfg = config.load()
    local out_dir = env.output_dir(cfg and cfg.turbo and cfg.turbo.output_dir)
    if not M.reapply_enabled(out_dir) then return true, "off (kill switch)" end
    local tm = require 'imports/turbo/features/team_mass'
    local rule = tm.load_role_rule(out_dir)
    if not rule then return true, "no saved role rule" end
    if not game.in_cm() then return false, "not in career mode" end
    local team = game.user_team_id()
    if team <= 0 then return false, "user team not found" end
    local TURBO = require 'imports/turbo/turbo'
    local ok, msg = TURBO.run("team_mass", { teamid = team, actions = { "squad_roles" }, role_rules = rule.rules,
        role_pins = rule.pins, role_save = false }, { silent = true })
    log.info("squad_role: re-applied the saved rule after event %s: %s%s", tostring(event_id), ok and "" or "NOT done - ", tostring(msg))
    return ok == true, msg
end

-- Career event tap: event 23 arms the re-apply, it is tried at once and then on the next career events until it succeeds.
function M.on_event(event_id)
    local events = require 'imports/turbo/core/events'
    if events.is_synthetic(event_id) then return end
    TURBO_STATE.role_reapply = TURBO_STATE.role_reapply or { pending = 0 }
    local st = TURBO_STATE.role_reapply
    if event_id == M.SEASON_RESET then st.pending = M.REAPPLY_TRIES end
    if (st.pending or 0) <= 0 then return end
    st.pending = st.pending - 1
    local ok, done = pcall(reapply_now, event_id)
    if ok and done then st.pending = 0 end
    if not ok then log.error("squad_role re-apply crashed: %s", tostring(done)) end
end

-- Installs the tap (pure Lua: no game call, no memory read, so it is safe at game launch). Called by TURBO.boot.
function M.install_reapply()
    local events = require 'imports/turbo/core/events'
    events.set_tap("squad_role_reapply", M.on_event)
end

return M
