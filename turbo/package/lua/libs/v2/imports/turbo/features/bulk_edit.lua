-- Turbo feature: bulk edit players (FC 26 LE v26.3.2 / v26.3.5 "Bulk Edit Players").
--   "bulk_edit": {
--     "scope":   { "user_team": true },             -- or teamids / playerids / all (+ "confirm_all": true)
--     "filters": { "max_age": 21, "min_potential": 80 },
--     "set":     { "potential": 90, "isretiring": 0 },  -- any players-table fields, range-checked
--     "actions": { "fitness": 95, "form": 100, "morale": 100,
--                  "development": { "xp_multiplier": 2.0, "bonus_xp": 0, "no_decline": true } }
--   }
-- Every field and value is validated before the first write; one bad entry stops the whole run.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local sel = require 'imports/turbo/core/select'

local M = {}

local ACTION_RANGES = {
    form = { 0, 100, "SetPlayerForm" },
    morale = { 0, 100, "SetPlayerMorale" },
    fitness = { 5, 95, "SetPlayerFitness" },
}

local function plan_actions(actions)
    local steps = {}
    actions = type(actions) == "table" and actions or {}
    for key, v in pairs(actions) do
        if key == "development" then
            if type(v) ~= "table" then return nil, "actions.development must be an object" end
            local mult = tonumber(v.xp_multiplier or 1.0)
            local bonus = util.to_int(v.bonus_xp or 0)
            local no_decline = v.no_decline == true
            if not mult or mult < 0 or mult > 100 then return nil, "development.xp_multiplier must be 0..100" end
            if not bonus or bonus < 0 then return nil, "development.bonus_xp must be an integer >= 0" end
            if type(PlayerDevelopmentManagerAddPlayer) ~= "function" or type(PlayerDevelopmentManagerSave) ~= "function" then
                return nil, "player development natives are not available in this Live Editor build"
            end
            steps[#steps + 1] = { kind = "development", mult = mult + 0.0, bonus = bonus, no_decline = no_decline }
        elseif ACTION_RANGES[key] then
            local r = ACTION_RANGES[key]
            local iv = util.to_int(v)
            if iv == nil or iv < r[1] or iv > r[2] then return nil, string.format("actions.%s must be %d..%d", key, r[1], r[2]) end
            if type(_G[r[3]]) ~= "function" then return nil, r[3] .. " is not available in this Live Editor build" end
            steps[#steps + 1] = { kind = "api", fn = r[3], value = iv, key = key }
        else
            return nil, "unknown action: " .. tostring(key)
        end
    end
    table.sort(steps, function(a, b) return (a.key or a.kind) < (b.key or b.kind) end)
    return steps
end

function M.run(ctx)
    local cfg = ctx.cfg
    local list, players_or_err, desc = sel.players(cfg.scope, cfg.filters, { allow_all = cfg.confirm_all == true })
    if not list then return false, players_or_err end
    local players = players_or_err

    -- Validate field writes
    local writes = {}
    for field, value in pairs(type(cfg.set) == "table" and cfg.set or {}) do
        if field == "playerid" then return false, "playerid cannot be bulk-edited" end
        local v, verr = db.validate(players, field, value)
        if v == nil then return false, verr end
        writes[#writes + 1] = { field = field, value = v }
    end
    table.sort(writes, function(a, b) return a.field < b.field end)

    local steps, serr = plan_actions(cfg.actions)
    if not steps then return false, serr end
    if #writes == 0 and #steps == 0 then return false, "nothing to do: \"set\" and \"actions\" are empty" end
    if #steps > 0 and not game.in_cm() then return false, "actions (fitness/form/morale/development) need a loaded career" end
    if #list == 0 then return true, "no players matched " .. tostring(desc) end

    local api_fail, dev_added = 0, 0
    for _, p in ipairs(list) do
        for _, w in ipairs(writes) do
            local ok, werr = db.set(players, p.rec, w.field, w.value, ctx.dry)
            if not ok then return false, string.format("player %d: %s", p.pid, werr) end
        end
        if not ctx.dry then
            for _, s in ipairs(steps) do
                if s.kind == "api" then
                    if not pcall(_G[s.fn], p.pid, s.value) then api_fail = api_fail + 1 end
                elseif s.kind == "development" then
                    if pcall(PlayerDevelopmentManagerAddPlayer, p.pid, s.mult, s.bonus, s.no_decline) then
                        dev_added = dev_added + 1
                    else
                        api_fail = api_fail + 1
                    end
                end
            end
        end
    end
    if dev_added > 0 then pcall(PlayerDevelopmentManagerSave) end

    local parts = {}
    for _, w in ipairs(writes) do parts[#parts + 1] = w.field .. "=" .. tostring(w.value) end
    for _, s in ipairs(steps) do
        if s.kind == "api" then parts[#parts + 1] = s.key .. "=" .. s.value
        else parts[#parts + 1] = string.format("development(x%.2f,+%d,%s)", s.mult, s.bonus, s.no_decline and "no decline" or "decline") end
    end
    local summary = string.format("%d players (%s): %s", #list, desc, table.concat(parts, ", "))
    if api_fail > 0 then summary = summary .. string.format("; %d API calls failed", api_fail) end
    return api_fail == 0, summary
end

return M
