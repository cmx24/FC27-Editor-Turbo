-- Turbo feature: reveal player data (Manager Career), like FC 26 Live Editor's "Reveal player data". The career hides
-- a player's true attributes and potential until your scouts have reported on him; Turbo asks the game to mark the
-- player fully scouted, so the Player Bio, the squad screens and the Global Transfer Network show the true values.
--   "reveal": { "scope": { "playerid": 158023 } | { "playerids": [..] } | { "teamid": 44 } | { "teamids": [..] } |
--                        { "user_team": true } | { "leagueid": 31 },
--               "confirm": false, "allow_evict": false }
--
-- How it works (docs/re/development.md): the career's PlayerDataRevealManager (Live Editor manager type 78) keeps one
-- reveal record per player you know about; 204 scouting points is "fully revealed". Turbo.dll calls the game's own
-- RevealPlayerFully / RevealTeamFully on the game thread (the functions the game runs itself when a player joins your
-- club or you take a club) through TurboRevealPlayerData(pdrm, mode, id) -> ok, message, status, out0, out1, a Lua
-- native the bridge defines once it finds Turbo.dll's turbo_game_call export. This module validates the request and
-- refuses when that native is missing (the Turbo window greys the buttons: caps key "reveal").
--
-- The game keeps at most 1500 records: at 1500 it drops the oldest down to 1400. A scope that would get there (about
-- three whole leagues) is refused unless "allow_evict" is true, so revealing a league never silently forgets players
-- your scouts reported on.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local mem = require 'imports/turbo/core/mem'
local moves = require 'imports/turbo/core/moves'

local M = {}

M.MANAGER_TYPE = 78   -- ENUM_FCEGameModesFCECareerModePlayerDataRevealManager
M.NATIVE = "TurboRevealPlayerData"
M.RECORDS = 0x790     -- record vector {begin, end, capacity} in the manager (docs/re/development.md)
M.RECORD_SIZE = 0x14
M.MAX_RECORDS = 1500  -- the game evicts down to 1400 when an insert reaches this
M.FULL_POINTS = 204

local function team_name(tid)
    local ok, name = pcall(game.team_name, tid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return "team " .. tostring(tid)
end

local function int_list(v)
    local out = {}
    if type(v) ~= "table" then return out end
    for _, x in ipairs(v) do
        local i = util.to_int(x)
        if i and i > 0 then out[#out + 1] = i end
    end
    return out
end

-- Players of each team (club links in teamplayerlinks): { [teamid] = count }
local function squad_sizes(teamids)
    local want, out = {}, {}
    for _, t in ipairs(teamids) do want[t] = true; out[t] = 0 end
    local links = db.get_table("teamplayerlinks")
    if not links or not db.has_fields(links, { "teamid", "playerid" }) then return out end
    for rec in db.records(links) do
        local t = links:GetRecordFieldValue(rec, "teamid")
        if want[t] then out[t] = out[t] + 1 end
    end
    return out
end

-- Teams of a league (leagueteamlinks), sorted
local function league_teams(leagueid)
    local links, err = db.get_table("leagueteamlinks")
    if not links then return nil, err end
    if not db.has_fields(links, { "leagueid", "teamid" }) then return nil, "leagueteamlinks has no leagueid / teamid fields" end
    local out, seen = {}, {}
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "leagueid") == leagueid then
            local t = links:GetRecordFieldValue(rec, "teamid")
            if t and t > 0 and not seen[t] then seen[t] = true; out[#out + 1] = t end
        end
    end
    table.sort(out)
    return out
end

-- Reveal records the manager holds now (nil when it cannot be read: the DLL validates again anyway)
function M.record_count(pdrm)
    if not pdrm then return nil end
    local _, _, count = mem.vector(pdrm + M.RECORDS, M.RECORD_SIZE, M.MAX_RECORDS)
    return count
end

-- Validates the request. Returns plan { players = {..}, teams = {..}, label, new_players } or nil, reason.
function M.plan(cfg)
    if not game.in_cm() then return nil, "revealing player data needs a loaded Manager Career" end
    local scope = type(cfg.scope) == "table" and cfg.scope or {}
    local players, teams, label = {}, {}, nil
    local pid = util.to_int(scope.playerid)
    if pid and pid > 0 then
        players = { pid }
    elseif scope.playerids ~= nil then
        players = int_list(scope.playerids)
    elseif scope.teamid ~= nil then
        teams = int_list({ scope.teamid })
    elseif scope.teamids ~= nil then
        teams = int_list(scope.teamids)
    elseif scope.leagueid ~= nil then
        local lid = util.to_int(scope.leagueid)
        if not lid or lid <= 0 then return nil, "scope.leagueid must be a positive whole number" end
        local list, err = league_teams(lid)
        if not list then return nil, err end
        if #list == 0 then return nil, string.format("league %d has no teams in leagueteamlinks", lid) end
        teams = list
        label = string.format("league %d (%d clubs)", lid, #list)
    elseif scope.user_team == true then
        local mine = moves.user_team()
        if mine == 0 then return nil, "your club is unknown (is the Turbo window running?)" end
        teams = { mine }
        label = "your club " .. team_name(mine)
    else
        return nil, "scope must be playerid, playerids, teamid, teamids, leagueid or user_team"
    end
    if #players == 0 and #teams == 0 then return nil, "no player or team id given (ids must be positive whole numbers)" end
    if #players > 200 then return nil, "at most 200 players per run: reveal their clubs instead" end
    if #teams > 60 then return nil, "at most 60 clubs per run" end

    if #players > 0 then
        local tbl, err = db.get_table("players")
        if not tbl then return nil, err end
        local known = {}
        for rec in db.records(tbl) do known[tbl:GetRecordFieldValue(rec, "playerid")] = true end
        for _, p in ipairs(players) do
            -- the game's RevealPlayerFully does nothing for a player id without a players row
            if not known[p] then return nil, string.format("player %d is not in the players table", p) end
        end
        label = label or (#players == 1 and string.format("%s (%d)", game.player_name(players[1]), players[1])
            or string.format("%d players", #players))
    end
    local new_players = #players
    if #teams > 0 then
        local tbl, err = db.get_table("teams")
        if not tbl then return nil, err end
        for _, t in ipairs(teams) do
            if not db.find(tbl, "teamid", t) then return nil, string.format("team %d is not in the teams table", t) end
        end
        local sizes = squad_sizes(teams)
        for _, t in ipairs(teams) do new_players = new_players + (sizes[t] or 0) end
        label = label or (#teams == 1 and string.format("%s (%d)", team_name(teams[1]), teams[1]) or string.format("%d clubs", #teams))
    end
    -- several clubs at once (a league) is a big change to the career's scouting knowledge: confirm it
    if #teams > 1 and cfg.confirm ~= true then
        return nil, string.format("would reveal every player of %s (about %d players); set \"confirm\": true to do it", label, new_players)
    end
    return { players = players, teams = teams, label = label, new_players = new_players }
end

function M.run(ctx)
    local cfg = ctx.cfg or {}
    local plan, why = M.plan(cfg)
    if not plan then return false, why end
    local native, reason = env.api(M.NATIVE)
    if ctx.dry then
        return true, string.format("dry run: %s would be revealed (about %d players)%s", plan.label, plan.new_players,
            native and "" or "; Turbo.dll's game call is not available right now")
    end
    if not native then
        return false, "revealing player data needs Turbo.dll's game-call support, which is not available right now (" ..
            tostring(reason) .. ")"
    end
    -- the manager: Lua's walk of the manager table when the memory map is there; else 0 = the pointer Turbo.dll saw in
    -- the game's own events (the DLL validates either one: vtable, size, the hub's own slot, slot 78)
    local pdrm = mem.map_available() and mem.manager(M.MANAGER_TYPE) or nil
    local before = M.record_count(pdrm)
    if before and before + plan.new_players >= M.MAX_RECORDS - 1 and cfg.allow_evict ~= true then
        return false, string.format("the career already holds %d reveal records; %d more would reach the game's limit of %d " ..
            "and it would forget the oldest scouting reports. Reveal fewer clubs, or set \"allow_evict\": true", before,
            plan.new_players, M.MAX_RECORDS)
    end
    local jobs = {}
    for _, p in ipairs(plan.players) do jobs[#jobs + 1] = { mode = 0, id = p, what = "player " .. p } end
    for _, t in ipairs(plan.teams) do jobs[#jobs + 1] = { mode = 1, id = t, what = team_name(t) } end
    local done, msgs = 0, {}
    for i, j in ipairs(jobs) do
        local ok, res, msg, status = pcall(native, pdrm or 0, j.mode, j.id)
        if not ok then return false, string.format("reveal %s failed: %s (%d of %d done)", j.what, tostring(res), done, #jobs) end
        if res ~= true then
            return false, string.format("reveal %s failed: %s (%d of %d done)", j.what, tostring(msg or res), done, #jobs)
        end
        if status == "queued" then
            -- not on the game thread (Live Editor's script runner): Turbo.dll runs this one on the next game tick; the
            -- rest needs another run (one game call at a time)
            local left = #jobs - i
            return true, string.format("reveal %s queued for the game thread: %s%s", j.what, tostring(msg),
                left > 0 and string.format(" (%d more to go: run it again after the next career-mode event)", left) or "")
        end
        done = done + 1
        if #jobs <= 3 then msgs[#msgs + 1] = tostring(msg) end
    end
    local after = M.record_count(pdrm)
    local tail = (before and after) and string.format("; reveal records %d -> %d", before, after) or ""
    if #msgs > 0 then
        return true, string.format("revealed %s: %s%s", plan.label, table.concat(msgs, "; "), tail)
    end
    return true, string.format("revealed %s: %d game calls%s. Open the Player Bio to see true attributes and potential",
        plan.label, done, tail)
end

return M
