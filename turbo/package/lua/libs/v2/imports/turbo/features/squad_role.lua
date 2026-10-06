-- Turbo feature: set the squad role of your club's players.
-- Port of FC 26 mass_edit_squadrole.lua. In FC 27 Live Editor, SetSquadRole is a "TODO: FC27" stub.
-- Turbo writes the role in two places when they exist:
--   1) career_playercontract.playerrole (FC 26 storage), and
--   2) the PlayerStatusManager role list in memory. FC 26 kept it as a vector of {playerid, role}
--      at manager+0x18. Turbo searches the manager for a vector whose entries match your squad,
--      so a moved offset or a grown entry is found again; nothing is written unless it matches.
-- Roles: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Prospect
--   "squad_role": { "role": 3, "include_loaned_in": false, "use_memory": true }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local calib = require 'imports/turbo/core/calib'
local log = require 'imports/turbo/core/log'

local M = {}

M.FC26_LAYOUT = { offset = 0x18, size = 8, role_off = 4 }
local ENTRY_SIZES = { 8, 12, 16 }
local MAX_ENTRIES = 400
local LOANED_IN = { [1] = true, [3] = true, [5] = true }

local function score_vector(base, count, size, role_off, squad)
    local matched, valid_roles = 0, 0
    for i = 0, count - 1 do
        local e = base + i * size
        local pid = mem.int(e)
        if pid and squad[pid] then
            matched = matched + 1
            -- FC 27 stores the role in ONE byte ({int pid, int8 role, u8 wasPromised, u8 renewalDismissed}): read the byte, a
            -- promised player's int would be 0x1xx (docs/re/player_status_roles.md section 3). 0xFF = no role yet.
            local role = mem.byte(e + role_off)
            if role and ((role >= 0 and role <= 5) or role == 0xFF) then valid_roles = valid_roles + 1 end
        end
    end
    return matched, valid_roles
end

-- Find the role vector. Returns layout {mgr, offset, size, role_off, begin, count, matched} or nil, reason
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
            local matched, valid = score_vector(b, count, t[2], t[3], squad)
            local density = matched / count
            -- Prefer more squad matches; on a tie prefer the entry size where squad players fill the
            -- vector densely (an 8-byte stride over 16-byte entries also "matches", at half density).
            if matched >= need and valid == matched and (best == nil or matched > best.matched
                or (matched == best.matched and density > best.density)) then
                best = { mgr = mgr, offset = t[1], size = t[2], role_off = t[3], begin = b, count = count,
                    matched = matched, density = density }
                if t == tries[1] and matched >= squad_count * 0.9 and density >= 0.9 then break end
            end
        end
    end
    if not best then return nil, "no role list matching your squad was found in PlayerStatusManager" end
    return best
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

-- Adds an entry for each player of your club who has none. role_of(pid) -> 1..5 (nil or out of range = Rotation 3).
-- Returns added (number), summary, missing_left (number); added = nil with the reason when nothing could be checked.
function M.repair_entries(role_of, dry)
    if not mem.map_available() then return nil, mem.NO_MAP end
    local user_team = game.user_team_id()
    if not user_team or user_team <= 0 then return nil, "user team not found" end
    local st, err = M.psm_state(user_team)
    if not st then return nil, err end
    local club, n = M.club_players(user_team)
    local missing = {}
    for pid in pairs(club) do
        if not st.pids[pid] then missing[#missing + 1] = pid end
    end
    table.sort(missing)
    if #missing == 0 then return 0, string.format("all %d players of your club have a squad status entry", n), 0 end
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
    local added, names = 0, {}
    for _, pid in ipairs(missing) do
        if st.count >= P.slots then break end
        local role = role_of and role_of(pid) or nil
        if math.type(role) ~= "integer" or role < 1 or role > 5 then role = 3 end
        local e = st.begin + st.count * P.size
        if not dry then
            MEMORY:WriteInt(e, pid)
            MEMORY:WriteBytes(e + 4, { role, 0, 0 })
            if mem.int(e) ~= pid then return added, string.format("squad status entry for player %d did not read back", pid), #missing - added end
            MEMORY:WriteInt(st.mgr + P.count, st.count + 1)
        end
        st.count = st.count + 1
        st.pids[pid] = st.count - 1
        added = added + 1
        names[#names + 1] = string.format("%d (role %d)", pid, role)
    end
    local left = #missing - added
    local text = string.format("squad status entries added for %d of %d players without one: %s", added, #missing, table.concat(names, ", "))
    if removed > 0 then text = text .. string.format(" (%d entries of players no longer at your club removed to make room)", removed) end
    if left > 0 then
        text = text .. string.format("; %d left out: the game's table is full (%d slots, entries of players who left stay until the game removes them)", left, P.slots)
    end
    return added, (dry and "[DRY RUN] " or "") .. text, left
end

function M.run(ctx)
    local role = util.to_int(ctx.cfg.role)
    if not role or role < 1 or role > 5 then return false, "role must be 1 (Crucial) .. 5 (Prospect)" end
    return M.apply(ctx, function() return role end, string.format("role %d", role))
end

-- Sets roles for your club's players: role_of(pid) -> 1..5, or nil to leave the player alone. `label` starts the summary.
-- (Turbo's team mass actions use it with a role by age.)
function M.apply(ctx, role_of, label)
    local squad, count, source = game.user_squad()
    if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end
    local skip = (ctx.cfg.include_loaned_in == true) and {} or loaned_in(squad, game.user_team_id())

    local parts = {}
    local wrote_any = false

    -- 1) career_playercontract
    local contracts = db.get_table("career_playercontract")
    if contracts and db.has_fields(contracts, { "playerid", "playerrole", "contract_status" }) then
        local n = 0
        for rec in db.records(contracts) do
            local pid = contracts:GetRecordFieldValue(rec, "playerid")
            local role = squad[pid] and not skip[pid] and role_of(pid) or nil
            if role and not LOANED_IN[contracts:GetRecordFieldValue(rec, "contract_status")] then
                local v, verr = db.validate(contracts, "playerrole", role)
                if v == nil then return false, verr end
                local wok, werr = db.set(contracts, rec, "playerrole", role, ctx.dry)
                if not wok then return false, werr end
                n = n + 1
            end
        end
        parts[#parts + 1] = string.format("career_playercontract: %d rows", n)
        wrote_any = wrote_any or n > 0
    else
        parts[#parts + 1] = "career_playercontract: not present"
    end

    -- 2) PlayerStatusManager memory
    local layout, lerr = nil, "memory pass disabled (use_memory = false)"
    if ctx.cfg.use_memory ~= false then
        if mem.map_available() then
            -- players of your club without an entry get one first, with the role asked for (FC 27 layout only)
            local added, rtext, left = M.repair_entries(function(pid) return not skip[pid] and role_of(pid) or nil end, ctx.dry)
            if added and (added > 0 or (left or 0) > 0) then
                parts[#parts + 1] = rtext
                wrote_any = wrote_any or added > 0
            end
            layout, lerr = M.locate(squad, count, calib.get(ctx.out_dir, "squad_role"))
        else
            lerr = mem.NO_MAP
        end
    end
    if layout then
        local n = 0
        for i = 0, layout.count - 1 do
            local e = layout.begin + i * layout.size
            local pid = mem.int(e)
            local role = pid and squad[pid] and not skip[pid] and role_of(pid) or nil
            if role then
                if not ctx.dry then MEMORY:WriteBytes(e + layout.role_off, { role }) end   -- one byte: the two flags after it stay
                n = n + 1
            end
        end
        parts[#parts + 1] = string.format("PlayerStatusManager +0x%X (entry %d bytes): %d players", layout.offset, layout.size, n)
        wrote_any = wrote_any or n > 0
        calib.put(ctx.out_dir, "squad_role", { offset = layout.offset, size = layout.size, role_off = layout.role_off })
    else
        parts[#parts + 1] = "PlayerStatusManager: " .. tostring(lerr)
        log.warn("squad_role memory pass skipped: %s", tostring(lerr))
    end

    if util.count(skip) > 0 then parts[#parts + 1] = string.format("%d loaned-in players skipped", util.count(skip)) end
    local summary = label .. "; " .. table.concat(parts, "; ")
    if not wrote_any then return false, summary end
    return true, summary
end

return M
