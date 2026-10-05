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
        tries[#tries + 1] = { util.to_int(hint.offset), util.to_int(hint.size), util.to_int(hint.role_off) or 4 }
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
