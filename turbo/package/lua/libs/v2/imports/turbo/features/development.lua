-- Turbo feature: player development (Manager Career), FC 26 Live Editor's "Development" for FC 27.
-- FC 27 Live Editor v27.1.2 has no PlayerDevelopmentManagerAddPlayer (FC 26's XP multiplier). What it has are the
-- natives of the game's own development plans (DOC.MD): every player of the user's club has a plan whose attribute
-- values take priority over the players table, PlayerHasDevelopementPlan(pid) / PlayerSetValueInDevelopementPlan(pid,
-- field, value). Turbo grows players by writing the attributes into the players table AND into the plan when the
-- player has one, so the game's own growth continues from the new values instead of putting the old ones back.
--
-- Action (button / script):
--   "development": { "scope": { "playerid": N } | { "playerids": [..] } | { "teamid": N } | { "user_team": true },
--                    "mode": "to_potential" | "add" | "set" | "none",
--                    "delta": 0,            -- "add": points added to each attribute of the player's group
--                    "attributes": [] | {}, -- "add": names (empty = the group); "set": { name = value }
--                    "potential": 0,        -- 1..99 = set potential first (0 = leave)
--                    "growthprofile": -1,   -- >= 0 = set players.growthprofile (-1 = leave)
--                    "confirm": false }     -- needed for a club or more than one player
--   to_potential: every attribute of the player's group (goalkeeping for a goalkeeper, the rest for outfielders) rises
--   by potential - overall (capped at 99) and the overall becomes the potential: "develop to potential" at once.
--
-- Automatic forced growth (auto, like FC 26's AddPlayer(pid, xp_multiplier, bonus_xp, no_decline)):
--   "auto": { "development": { "enabled": false, "events": ["WEEK_PASSED", "POST_LOAD_PREPARE"],
--             "players": [], "user_team": false, "weekly": 1, "no_decline": true } }
--   every week each listed player (and the whole squad with user_team) gains "weekly" points on each attribute of his
--   group while his overall is below his potential; with no_decline an attribute that dropped since the last week is
--   put back (age decline). The snapshot for no_decline is kept for this game session only and starts over when a
--   career is loaded.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local log = require 'imports/turbo/core/log'
local events = require 'imports/turbo/core/events'
local moves = require 'imports/turbo/core/moves'

local M = {}

M.GK_ATTRS = { "gkdiving", "gkhandling", "gkkicking", "gkreflexes", "gkpositioning", "reactions" }
M.ATTRS = { "acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength", "reactions",
    "aggression", "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling",
    "finishing", "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "defensiveawareness", "shotpower",
    "longshots", "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling",
    "gkkicking", "gkreflexes", "gkpositioning" }
local IS_ATTR = util.set_of(M.ATTRS)
local IS_GK_ONLY = util.set_of({ "gkdiving", "gkhandling", "gkkicking", "gkreflexes", "gkpositioning" })
M.MAX = 99

TURBO_STATE = TURBO_STATE or { listeners = {} }
TURBO_STATE.development = TURBO_STATE.development or { snap = {} }

-- The attributes "to_potential" / "add" / weekly growth raise for a player (only fields the table has)
function M.group(tbl, position)
    local out = {}
    if position == 0 then
        for _, a in ipairs(M.GK_ATTRS) do if db.has_field(tbl, a) then out[#out + 1] = a end end
    else
        for _, a in ipairs(M.ATTRS) do if not IS_GK_ONLY[a] and db.has_field(tbl, a) then out[#out + 1] = a end end
    end
    return out
end

-- The player's development plan natives (FC 27 LE v27.1.2), nil when this build has none
local function plan_natives()
    if type(PlayerHasDevelopementPlan) == "function" and type(PlayerSetValueInDevelopementPlan) == "function" then
        return PlayerHasDevelopementPlan, PlayerSetValueInDevelopementPlan
    end
    return nil
end

-- Writes attribute values into the player's development plan when he has one. Returns "plan", "none" or "failed: .."
local function sync_plan(pid, attrs)
    local has, set = plan_natives()
    if not has then return "unavailable" end
    local ok, yes = pcall(has, pid)
    if not ok then return "failed: " .. tostring(yes) end
    if yes ~= true then return "none" end
    for field, v in pairs(attrs) do
        local ok2, err = pcall(set, pid, field, v)
        if not ok2 then return "failed: " .. tostring(err) end
    end
    return "plan"
end

-- players row of each id: { [pid] = rec }
local function player_rows(tbl, ids)
    local want, out = util.set_of(ids), {}
    for rec in db.records(tbl) do
        local pid = tbl:GetRecordFieldValue(rec, "playerid")
        if want[pid] then out[pid] = rec end
    end
    return out
end

-- Player ids of a scope, label (or nil, reason)
function M.scope_players(scope)
    scope = type(scope) == "table" and scope or {}
    local pid = util.to_int(scope.playerid)
    if pid and pid > 0 then return { pid }, string.format("%s (%d)", game.player_name(pid), pid) end
    if type(scope.playerids) == "table" then
        local out = {}
        for _, x in ipairs(scope.playerids) do
            local i = util.to_int(x)
            if i and i > 0 then out[#out + 1] = i end
        end
        return out, string.format("%d players", #out)
    end
    local tid = util.to_int(scope.teamid)
    if tid and tid <= 0 then tid = nil end
    if scope.user_team == true then
        tid = moves.user_team()
        if tid == 0 then return nil, "your club is unknown (is the Turbo window running?)" end
    end
    if tid then
        local links, err = db.get_table("teamplayerlinks")
        if not links then return nil, err end
        local out, seen = {}, {}
        for rec in db.records(links) do
            if links:GetRecordFieldValue(rec, "teamid") == tid then
                local p = links:GetRecordFieldValue(rec, "playerid")
                if p and p > 0 and not seen[p] then seen[p] = true; out[#out + 1] = p end
            end
        end
        table.sort(out)
        return out, string.format("%s (%d players)", game.team_name(tid), #out)
    end
    return nil, "scope must be playerid, playerids, teamid or user_team"
end

-- Plans the writes for one player: { pid, rec, fields = { name = value }, attrs = { name = value } } or nil, reason
local function plan_player(tbl, rec, pid, cfg)
    local get = function(f) return db.has_field(tbl, f) and tbl:GetRecordFieldValue(rec, f) or nil end
    local fields, attrs = {}, {}
    local pot = get("potential") or 0
    local ovr = get("overallrating") or 0
    local newpot = util.to_int(cfg.potential) or 0
    if newpot ~= 0 then
        if newpot < 1 or newpot > M.MAX then return nil, "potential must be 1..99 (0 = leave alone)" end
        fields.potential = newpot
        pot = newpot
    end
    local gp = util.to_int(cfg.growthprofile)
    if gp == nil then gp = -1 end
    if gp >= 0 then
        if not db.has_field(tbl, "growthprofile") then return nil, "this database has no players.growthprofile field" end
        fields.growthprofile = gp
    end
    local mode = cfg.mode or "none"
    local function raise(list, by)
        for _, a in ipairs(list) do
            local v = get(a)
            if v then
                local nv = math.max(1, math.min(M.MAX, v + by))
                if nv ~= v then attrs[a] = nv end
            end
        end
    end
    if mode == "to_potential" then
        local gap = pot - ovr
        if gap > 0 then
            raise(M.group(tbl, get("preferredposition1")), gap)
            fields.overallrating = math.min(M.MAX, pot)
        end
    elseif mode == "add" then
        local delta = util.to_int(cfg.delta) or 0
        if delta == 0 or delta < -50 or delta > 50 then return nil, "delta must be -50..50 and not 0" end
        local list = {}
        if type(cfg.attributes) == "table" and #cfg.attributes > 0 then
            for _, a in ipairs(cfg.attributes) do
                if not IS_ATTR[a] then return nil, "unknown attribute " .. tostring(a) end
                list[#list + 1] = a
            end
        else
            list = M.group(tbl, get("preferredposition1"))
        end
        raise(list, delta)
        if ovr > 0 and #list >= 6 then fields.overallrating = math.max(1, math.min(M.MAX, ovr + delta)) end
    elseif mode == "set" then
        if type(cfg.attributes) ~= "table" or next(cfg.attributes) == nil or #cfg.attributes > 0 then
            return nil, "mode \"set\" needs attributes as an object { name = value }"
        end
        for a, v in pairs(cfg.attributes) do
            if not IS_ATTR[a] then return nil, "unknown attribute " .. tostring(a) end
            local iv = util.to_int(v)
            if not iv or iv < 1 or iv > M.MAX then return nil, string.format("%s must be 1..99", a) end
            if db.has_field(tbl, a) then attrs[a] = iv end
        end
    elseif mode ~= "none" then
        return nil, "mode must be to_potential, add, set or none"
    end
    for f, v in pairs(fields) do
        local _, err = db.validate(tbl, f, v)
        if err then return nil, err end
    end
    for f, v in pairs(attrs) do
        local _, err = db.validate(tbl, f, v)
        if err then return nil, err end
    end
    return { pid = pid, rec = rec, fields = fields, attrs = attrs }
end

-- Applies planned writes. Returns written fields, plan state
local function apply_player(tbl, p, dry)
    local n = 0
    for f, v in pairs(p.fields) do
        if db.set(tbl, p.rec, f, v, dry) then n = n + 1 end
    end
    for f, v in pairs(p.attrs) do
        if db.set(tbl, p.rec, f, v, dry) then n = n + 1 end
    end
    if dry or next(p.attrs) == nil then return n, "none" end
    return n, sync_plan(p.pid, p.attrs)
end

function M.plan(cfg)
    if not game.in_cm() then return nil, "player development needs a loaded Manager Career" end
    local ids, label = M.scope_players(cfg.scope)
    if not ids then return nil, label end
    if #ids == 0 then return nil, "no players in that scope" end
    if #ids > 1 and cfg.confirm ~= true then
        return nil, string.format("would change %d players (%s); set \"confirm\": true to do it", #ids, label)
    end
    local tbl, err = db.get_table("players")
    if not tbl then return nil, err end
    local rows = player_rows(tbl, ids)
    local plans = {}
    for _, pid in ipairs(ids) do
        local rec = rows[pid]
        if not rec then return nil, string.format("player %d is not in the players table", pid) end
        local p, why = plan_player(tbl, rec, pid, cfg)
        if not p then return nil, string.format("player %d: %s", pid, why) end
        plans[#plans + 1] = p
    end
    return { tbl = tbl, plans = plans, label = label }
end

function M.run(ctx)
    local cfg = ctx.cfg or {}
    local plan, why = M.plan(cfg)
    if not plan then return false, why end
    local writes, synced, failed, changed = 0, 0, {}, 0
    for _, p in ipairs(plan.plans) do
        local n, state = apply_player(plan.tbl, p, ctx.dry)
        writes = writes + n
        if n > 0 then changed = changed + 1 end
        if state == "plan" then synced = synced + 1
        elseif state:sub(1, 7) == "failed:" then failed[#failed + 1] = string.format("%d %s", p.pid, state) end
    end
    local msg = string.format("%s: %d players changed, %d fields written", plan.label, changed, writes)
    if synced > 0 then msg = msg .. string.format(", %d development plans updated", synced) end
    if not plan_natives() and writes > 0 then
        msg = msg .. "; this Live Editor build has no development-plan natives: the game may put your players' old values back"
    end
    if #failed > 0 then
        return false, msg .. "; development plan failed for " .. table.concat(failed, ", ")
    end
    if ctx.dry then return true, "dry run: " .. msg end
    return true, msg
end

-- ---------------------------------------------------------------- automatic forced growth
-- One growth step for the listed players. Returns players grown, attributes restored
function M.weekly(cfg, dry)
    local tbl = db.get_table("players")
    if not tbl then return 0, 0, "players table not available" end
    local ids = {}
    for _, x in ipairs(type(cfg.players) == "table" and cfg.players or {}) do
        local i = util.to_int(x)
        if i and i > 0 then ids[#ids + 1] = i end
    end
    if cfg.user_team == true then
        local squad = game.user_squad()
        for pid in pairs(squad) do ids[#ids + 1] = pid end
    end
    local weekly = util.to_int(cfg.weekly) or 0
    if weekly < 0 or weekly > 5 then return 0, 0, "weekly must be 0..5" end
    local snap = TURBO_STATE.development.snap
    local rows = player_rows(tbl, ids)
    local grown, restored = 0, 0
    for _, pid in ipairs(ids) do
        local rec = rows[pid]
        if rec then
            local attrs = {}
            local s = snap[pid]
            if cfg.no_decline == true and s then
                for a, v in pairs(s) do
                    local cur = tbl:GetRecordFieldValue(rec, a)
                    if cur and cur < v then attrs[a] = v; restored = restored + 1 end
                end
            end
            local pot = db.get(tbl, rec, "potential") or 0
            local ovr = db.get(tbl, rec, "overallrating") or 0
            if weekly > 0 and ovr < pot then
                for _, a in ipairs(M.group(tbl, db.get(tbl, rec, "preferredposition1"))) do
                    local cur = attrs[a] or tbl:GetRecordFieldValue(rec, a)
                    if cur and cur < M.MAX then attrs[a] = math.min(M.MAX, cur + weekly) end
                end
                if not dry then db.set(tbl, rec, "overallrating", math.min(pot, ovr + weekly)) end
                grown = grown + 1
            end
            if not dry then
                for a, v in pairs(attrs) do db.set(tbl, rec, a, v) end
                if next(attrs) ~= nil then sync_plan(pid, attrs) end
            end
            local now = {}
            for _, a in ipairs(M.ATTRS) do
                local v = db.get(tbl, rec, a)
                if v then now[a] = (cfg.no_decline == true) and math.max(v, s and s[a] or 0) or v end
            end
            snap[pid] = now
        end
    end
    return grown, restored
end

function M.auto(ctx, event_id)
    local load_id = events.resolve("POST_LOAD_PREPARE")
    if event_id == -1 or event_id == load_id then
        -- armed now or a (re)loaded career: the snapshot of another career means nothing here. Take a fresh one and
        -- grow nobody (growth only on the configured week events)
        TURBO_STATE.development.snap = {}
        local cfg = util.deep_copy(ctx.cfg or {})
        cfg.weekly, cfg.no_decline = 0, false
        M.weekly(cfg, ctx.dry)
        log.debug("development: no-decline snapshot taken (event %s)", tostring(event_id))
        return
    end
    local grown, restored, err = M.weekly(ctx.cfg or {}, ctx.dry)
    if err then log.warn("development auto skipped: %s", err)
    else log.info("development auto (event %s): %d players grew, %d attributes put back", tostring(event_id), grown, restored) end
end

return M
