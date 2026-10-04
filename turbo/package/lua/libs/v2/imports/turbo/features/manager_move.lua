-- Turbo feature: the manager market in the career database, like FC 26 Live Editor's "transfer / fire manager".
-- Feature-flagged: nothing happens unless modules.manager_move.enabled is true.
--   "manager_move": { "enabled": false, "managerid": 0, "teamid": 0, "replacement": 0, "confirm": false }
--
--   teamid > 0   move manager `managerid` to that club. The club's current manager takes the mover's old job (a swap),
--                or becomes a free agent when the mover had no club: every club keeps exactly one manager.
--   teamid = 0   make the manager available: he becomes a free agent (manager.teamid 0) and a free-agent manager takes
--                his club (`replacement`, or the first free agent in the table when 0).
--
-- Why the database (docs/re/manager_rules.md section 5): the game's AI hiring reads its candidates straight from the
-- manager table (0x147B6E1C8 selects "managerid FROM manager WHERE teamid = 0 AND islicensed = .. ORDER BY
-- managerjointeamdate", the free agents) and the club pages read manager.teamid, so a database edit is what the game
-- itself reads; it is saved with the career. Your own club is never touched (your job changes through job offers,
-- the Managers tab's "Job offers"), and national teams are refused.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'

local M = {}

local function team_name(tid)
    local ok, name = pcall(game.team_name, tid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return "team " .. tostring(tid)
end

local function manager_name(tbl, rec, mid)
    local first = db.has_field(tbl, "firstname") and db.get(tbl, rec, "firstname") or ""
    local sur = db.has_field(tbl, "surname") and db.get(tbl, rec, "surname") or ""
    local name = util.trim(tostring(first or "") .. " " .. tostring(sur or ""))
    if name == "" then name = "manager " .. tostring(mid) end
    return name
end

-- every manager row: { rec, managerid, teamid, name }
local function rows(tbl)
    local out = {}
    for rec in db.records(tbl) do
        local mid = util.to_int(tbl:GetRecordFieldValue(rec, "managerid"))
        local tid = util.to_int(tbl:GetRecordFieldValue(rec, "teamid"))
        if mid then out[#out + 1] = { rec = rec, managerid = mid, teamid = tid or 0, name = manager_name(tbl, rec, mid) } end
    end
    return out
end

-- Validates the request. Returns plan { writes = { {rec, managerid, name, from, to} }, text } or nil, reason.
function M.plan(cfg)
    if cfg.enabled ~= true then
        return nil, "manager moves are off: set \"manager_move\": {\"enabled\": true} in turbo_config.json (feature flag)"
    end
    if not game.in_cm() then return nil, "moving managers needs a loaded career" end
    local mid = util.to_int(cfg.managerid)
    if not mid or mid <= 0 then return nil, "managerid must be a positive whole number" end
    local to = util.to_int(cfg.teamid)
    if not to or to < 0 then return nil, "teamid must be a club id, or 0 to make the manager a free agent" end
    local tbl, err = db.get_table("manager")
    if not tbl then return nil, err end
    local okf, missing = db.has_fields(tbl, { "managerid", "teamid" })
    if not okf then return nil, "the manager table has no " .. table.concat(missing, ", ") end
    local all = rows(tbl)
    local mover
    for _, r in ipairs(all) do if r.managerid == mid then mover = r break end end
    if not mover then return nil, string.format("manager %d is not in the manager table", mid) end
    local user = moves.user_team()
    if user <= 0 then return nil, "your club is not known (is the Turbo window running?)" end
    if mover.teamid == user then
        return nil, string.format("%s manages your club: your own job changes through job offers, not here", mover.name)
    end
    local nat = moves.national_teams()
    if mover.teamid > 0 and nat[mover.teamid] then
        return nil, string.format("%s manages a national team (%s): Turbo moves club managers only", mover.name, team_name(mover.teamid))
    end
    local writes = {}
    local text
    if to > 0 then
        if to == mover.teamid then return nil, string.format("%s already manages %s", mover.name, team_name(to)) end
        if to == user then return nil, "that is your own club: Turbo does not give your job to another manager" end
        local teams, terr = db.get_table("teams")
        if not teams then return nil, terr end
        if not db.find(teams, "teamid", to) then return nil, string.format("team %d is not in the teams table", to) end
        if nat[to] then return nil, string.format("%s is a national team: Turbo moves club managers only", team_name(to)) end
        local displaced = {}
        for _, r in ipairs(all) do if r.teamid == to and r.managerid ~= mid then displaced[#displaced + 1] = r end end
        writes[#writes + 1] = { rec = mover.rec, managerid = mid, name = mover.name, from = mover.teamid, to = to }
        local names = {}
        for _, r in ipairs(displaced) do
            writes[#writes + 1] = { rec = r.rec, managerid = r.managerid, name = r.name, from = to, to = mover.teamid }
            names[#names + 1] = r.name
        end
        text = string.format("%s -> %s", mover.name, team_name(to))
        if #displaced > 0 then
            text = text .. string.format("; %s -> %s", table.concat(names, ", "),
                mover.teamid > 0 and team_name(mover.teamid) or "free agent")
        end
    else
        if mover.teamid <= 0 then return nil, string.format("%s is already a free agent", mover.name) end
        local rid = util.to_int(cfg.replacement) or 0
        local repl
        for _, r in ipairs(all) do
            if r.managerid ~= mid and r.teamid == 0 and (rid == 0 or r.managerid == rid) then repl = r break end
        end
        if not repl then
            if rid ~= 0 then return nil, string.format("manager %d is not a free agent (manager.teamid 0)", rid) end
            return nil, "there is no free-agent manager (manager.teamid 0) to take over " .. team_name(mover.teamid)
        end
        writes[#writes + 1] = { rec = mover.rec, managerid = mid, name = mover.name, from = mover.teamid, to = 0 }
        writes[#writes + 1] = { rec = repl.rec, managerid = repl.managerid, name = repl.name, from = 0, to = mover.teamid }
        text = string.format("%s -> free agent; %s -> %s", mover.name, repl.name, team_name(mover.teamid))
    end
    -- every value fits the field (validated before anything is written)
    for _, w in ipairs(writes) do
        local okv, verr = db.set(tbl, w.rec, "teamid", w.to, true)
        if not okv then return nil, tostring(verr) end
    end
    if cfg.confirm ~= true then
        return nil, string.format("would move: %s; set \"confirm\": true to do it", text)
    end
    return { table = tbl, writes = writes, text = text }
end

function M.run(ctx)
    local plan, why = M.plan(ctx.cfg or {})
    if not plan then return false, why end
    if ctx.dry then return true, "dry run: would move " .. plan.text end
    local done = {}
    for _, w in ipairs(plan.writes) do
        local ok, err = db.set(plan.table, w.rec, "teamid", w.to, false)
        if not ok then
            -- put back what was already written: never leave a half swap
            for i = #done, 1, -1 do db.set(plan.table, done[i].rec, "teamid", done[i].from, false) end
            return false, string.format("manager %d: %s (nothing changed)", w.managerid, tostring(err))
        end
        done[#done + 1] = w
    end
    return true, "moved: " .. plan.text .. " (career database; the club pages show it, the save keeps it)"
end

return M
