-- Turbo feature: set the squad role of your club's players.
-- Port of FC 26 mass_edit_squadrole.lua. In FC 27 Live Editor, SetSquadRole is a "TODO: FC27" stub.
-- Turbo writes the role in two places when they exist:
--   1) career_playercontract.playerrole (FC 26 storage), and
--   2) the PlayerStatusManager role list in memory. FC 26 kept it as a vector of {playerid, role}
--      at manager+0x18. Turbo searches the manager for a vector whose entries match your squad,
--      so a moved offset or a grown entry is found again; nothing is written unless it matches.
-- Roles: 1 Crucial, 2 Important, 3 Rotation, 4 Sporadic, 5 Prospect
--   "squad_role": { "role": 3, "include_loaned_in": false, "use_memory": true }
--
-- Squad-role fix (docs/TURBO_2_0_PLAN.md section 2):
--   * the players to write are the entries of the role list itself (psm_members) that are also on your club's
--     teamplayerlinks; cm_teamsheets is only the anchor locate() scores the vector against (fix 1),
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

function M.run(ctx)
    local role = util.to_int(ctx.cfg.role)
    if not role or role < 1 or role > 5 then return false, "role must be 1 (Crucial) .. 5 (Prospect)" end
    local ok, summary = M.apply(ctx, function() return role end, string.format("role %d", role))
    if ok and not ctx.dry then
        -- one catch-all rule, so the role also survives the game's next season reset
        local tm = require 'imports/turbo/features/team_mass'
        tm.save_role_rule(ctx.out_dir, { { name = "all", role = role } }, {})
    end
    return ok, summary
end

local function add_count(parts, n, fmt)
    if n and n > 0 then parts[#parts + 1] = string.format(fmt, n) end
end

-- Sets roles for your club's players: role_of(pid) -> 1..5, or nil, reason to leave the player alone. `label` starts the
-- summary. (Turbo's team mass actions use it with a role by rule.)
-- ctx.preview = true (or ctx.dry): nothing is written. Returns ok, summary, report where report holds
--   {plan = {[pid]=role}, old = {[pid]=role byte}, reasons = {[reason]=count}, skipped = {[pid]=reason},
--    wrote = n, no_entry = {pids}, outside = {pids}, loaned = n, layout, source, squad_sheet, squad_links}
function M.apply(ctx, role_of, label)
    local squad, count, source, sinfo = game.user_squad()
    if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end
    local dry = ctx.dry == true or ctx.preview == true
    local user_team = game.user_team_id()
    -- the players that may be written: the user team's club links (the squad set also holds sheet-only players)
    local links = team_links(user_team)
    local targets = squad
    if links and next(links) ~= nil then targets = links end
    local skip = (ctx.cfg.include_loaned_in == true) and {} or loaned_in(targets, user_team)

    -- one decision per player, taken once (the rule engine may be asked for ages and ranks)
    local plan, reasons, skipped = {}, {}, {}
    local loaned_n = 0
    for pid in pairs(targets) do
        if skip[pid] then
            loaned_n = loaned_n + 1
            skipped[pid] = "loaned in"
        else
            local role, why = role_of(pid)
            if role then
                plan[pid] = role
            else
                why = why or "no role"
                reasons[why] = (reasons[why] or 0) + 1
                skipped[pid] = why
            end
        end
    end
    local report = { plan = plan, old = {}, reasons = reasons, skipped = skipped, wrote = 0, no_entry = {}, outside = {},
        loaned = loaned_n, source = source, squad_sheet = sinfo and sinfo.sheet or 0, squad_links = sinfo and sinfo.links or 0 }

    local parts = {}
    local wrote_any = false

    -- 1) career_playercontract
    local contracts = db.get_table("career_playercontract")
    if contracts and db.has_fields(contracts, { "playerid", "playerrole", "contract_status" }) then
        local n = 0
        for rec in db.records(contracts) do
            local pid = contracts:GetRecordFieldValue(rec, "playerid")
            local role = plan[pid]
            if role and not LOANED_IN[contracts:GetRecordFieldValue(rec, "contract_status")] then
                if report.old[pid] == nil then report.old[pid] = contracts:GetRecordFieldValue(rec, "playerrole") end
                local v, verr = db.validate(contracts, "playerrole", role)
                if v == nil then return false, verr end
                local wok, werr = db.set(contracts, rec, "playerrole", role, dry)
                if not wok then return false, werr end
                n = n + 1
            end
        end
        parts[#parts + 1] = string.format("career_playercontract: %d rows", n)
        wrote_any = wrote_any or n > 0
    else
        parts[#parts + 1] = "career_playercontract: not present"
    end

    -- 2) PlayerStatusManager memory: the role list's own entries, not the teamsheet's
    local layout, lerr = nil, "memory pass disabled (use_memory = false)"
    if ctx.cfg.use_memory ~= false then
        if mem.map_available() then
            layout, lerr = M.locate(squad, count, calib.get(ctx.out_dir, "squad_role"))
        else
            lerr = mem.NO_MAP
        end
    end
    if layout then
        local members, _, entries = M.psm_members(layout)
        local n = 0
        for _, ent in ipairs(entries) do
            local role = plan[ent.pid]
            report.old[ent.pid] = ent.role   -- the game's own state wins over the contract table
            if role then
                if not dry then MEMORY:WriteBytes(ent.addr, { role }) end   -- one byte: the two flags after it stay
                n = n + 1
            elseif not targets[ent.pid] then
                report.outside[#report.outside + 1] = ent.pid
            end
        end
        for pid in pairs(plan) do
            if not members[pid] then report.no_entry[#report.no_entry + 1] = pid end
        end
        table.sort(report.outside)
        table.sort(report.no_entry)
        report.wrote = n
        report.layout = layout
        parts[#parts + 1] = string.format("PlayerStatusManager +0x%X (entry %d bytes): %d players", layout.offset, layout.size, n)
        wrote_any = wrote_any or n > 0
        if not dry then
            calib.put(ctx.out_dir, "squad_role", { offset = layout.offset, size = layout.size, role_off = layout.role_off })
        end
        if #report.no_entry > 0 then
            log.warn("squad_role: no role list entry for players %s", table.concat(report.no_entry, ","))
        end
        if #report.outside > 0 then
            log.info("squad_role: role list players outside your club's links (left alone): %s", table.concat(report.outside, ","))
        end
    else
        parts[#parts + 1] = "PlayerStatusManager: " .. tostring(lerr)
        log.warn("squad_role memory pass skipped: %s", tostring(lerr))
    end

    if loaned_n > 0 then parts[#parts + 1] = string.format("%d loaned-in players skipped", loaned_n) end
    for _, why in ipairs(util.sorted_keys(reasons)) do
        parts[#parts + 1] = string.format("%d players skipped (%s)", reasons[why], why)
    end
    add_count(parts, #report.no_entry, "%d squad players have no entry in the role list")
    add_count(parts, #report.outside, "%d role list players are not in your squad (left alone)")
    local summary = label .. "; " .. table.concat(parts, "; ")
    if ctx.preview == true then summary = "[PREVIEW, nothing written] " .. summary end
    if not wrote_any then return false, summary, report end
    return true, summary, report
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
