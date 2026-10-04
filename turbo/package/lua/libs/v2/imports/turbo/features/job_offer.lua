-- Turbo feature: create a job offer for your manager from a chosen club (Manager Career), like FC 26 Live Editor's
-- "Create Job Offer". Feature-flagged: nothing happens unless modules.job_offer.enabled is true.
--   "job_offer": { "enabled": false, "teamid": 0, "confirm": false }
--
-- How it works (docs/re/job_offer.md): the game keeps your applications and the offers that answer them in the
-- career's JobMarketManager (Live Editor manager type 53). Turbo asks the game, on its own thread, to apply for the
-- job and to make the club answer at once (JobMarketManager::MakeOffer), so the inbox email and the Job Offers screen
-- come from the game's own code. The game call itself lives in Turbo.dll (hook foundation) and is reached through the
-- native TurboJobOfferCreate(jobmarket_address, teamid) -> ok, message. This module validates everything first and
-- refuses when that native is not present, so the Turbo window greys the button (caps key "job_offer").
--
-- v1 is club jobs only: national teams (teamnationlinks) are refused, as is your own club.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local mem = require 'imports/turbo/core/mem'
local moves = require 'imports/turbo/core/moves'

local M = {}

M.MANAGER_TYPE = 53   -- ENUM_FCEGameModesFCECareerModeJobMarketManager
M.NATIVE = "TurboJobOfferCreate"

local function team_name(tid)
    local ok, name = pcall(game.team_name, tid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return "team " .. tostring(tid)
end

-- Validates the request. Returns plan { teamid, name } or nil, reason.
function M.plan(cfg)
    if cfg.enabled ~= true then
        return nil, "job offers are off: set \"job_offer\": {\"enabled\": true} in turbo_config.json (feature flag)"
    end
    if not game.in_cm() then return nil, "creating a job offer needs a loaded Manager Career" end
    local tid = util.to_int(cfg.teamid)
    if not tid or tid <= 0 then return nil, "teamid must be a positive whole number" end
    local teams, err = db.get_table("teams")
    if not teams then return nil, err end
    if not db.find(teams, "teamid", tid) then return nil, string.format("team %d is not in the teams table", tid) end
    if moves.national_teams()[tid] then
        return nil, string.format("%s is a national team: Turbo creates club job offers only (v1)", team_name(tid))
    end
    local mine = moves.user_team()
    if mine == 0 then return nil, "your club is unknown (is the Turbo window running?)" end
    if tid == mine then return nil, "that is your own club" end
    if cfg.confirm ~= true then
        return nil, string.format("would create a job offer from %s (%d); set \"confirm\": true to do it", team_name(tid), tid)
    end
    return { teamid = tid, name = team_name(tid) }
end

function M.run(ctx)
    local plan, why = M.plan(ctx.cfg or {})
    if not plan then return false, why end
    if ctx.dry then
        return true, string.format("dry run: job offer from %s (%d) would be created", plan.name, plan.teamid)
    end
    local native, reason = env.api(M.NATIVE)
    if not native then
        return false, "job offer creation needs Turbo.dll's game-call support, which is not available in this Live Editor build (" ..
            tostring(reason) .. ")"
    end
    if not mem.map_available() then return false, mem.NO_MAP end
    local jmm = mem.manager(M.MANAGER_TYPE)
    if not jmm then return false, "the career's JobMarketManager was not found (load a Manager Career first)" end
    local ok, res, msg = pcall(native, jmm, plan.teamid)
    if not ok then return false, "job offer creation failed: " .. tostring(res) end
    if res ~= true then return false, "job offer creation failed: " .. tostring(msg or res) end
    return true, string.format("job offer created: %s (%d) wants you as manager; check your inbox and Job Offers",
        plan.name, plan.teamid)
end

return M
