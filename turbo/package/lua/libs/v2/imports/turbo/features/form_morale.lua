-- Turbo feature: keep the user squad's form / morale / fitness at set values.
-- Replaces FC 26 scripts auto_max_user_team_form_morale_sharpness.lua and
-- auto_max_user_team_sharpness.lua (match sharpness no longer exists in FC 27).
-- Config: auto.form_morale { enabled, form, morale, fitness, events }

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local log = require 'imports/turbo/core/log'

local M = {}

-- Ranges documented in Live Editor DOC.MD
local RANGES = {
    form = { 0, 100, "SetPlayerForm" },
    morale = { 0, 100, "SetPlayerMorale" },
    fitness = { 5, 95, "SetPlayerFitness" },
}

-- Returns list of {key, value, fn_name} to apply, or nil, error
local function plan(cfg)
    local out = {}
    for _, key in ipairs({ "form", "morale", "fitness" }) do
        local v = cfg[key]
        if v ~= nil and v ~= 0 and v ~= false then
            local iv = util.to_int(v)
            local r = RANGES[key]
            if iv == nil or iv < r[1] or iv > r[2] then
                return nil, string.format("%s must be 0 (leave alone) or %d..%d, got %s", key, r[1], r[2], tostring(v))
            end
            if type(_G[r[3]]) ~= "function" then
                return nil, r[3] .. " is not available in this Live Editor build"
            end
            out[#out + 1] = { key = key, value = iv, fn = r[3] }
        end
    end
    if #out == 0 then return nil, "nothing to apply: form, morale and fitness are all 0" end
    return out
end

local function apply(ctx)
    local steps, err = plan(ctx.cfg)
    if not steps then return false, err end
    local squad, count, source = game.user_squad()
    if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end
    local calls, failures = 0, 0
    if not ctx.dry then
        for pid in pairs(squad) do
            for _, s in ipairs(steps) do
                local ok = pcall(_G[s.fn], pid, s.value)
                if ok then calls = calls + 1 else failures = failures + 1 end
            end
        end
    end
    local parts = {}
    for _, s in ipairs(steps) do parts[#parts + 1] = s.key .. "=" .. s.value end
    local summary = string.format("%d players (%s): %s", count, source, table.concat(parts, ", "))
    if failures > 0 then summary = summary .. string.format("; %d calls failed", failures) end
    return failures == 0, summary
end

function M.run(ctx)
    return apply(ctx)
end

-- Called on career events (event_id = -1 for the immediate apply at boot)
function M.auto(ctx, event_id)
    local ok, summary = apply(ctx)
    if ok then
        log.debug("form_morale applied on event %s: %s", tostring(event_id), summary)
    else
        log.warn("form_morale skipped on event %s: %s", tostring(event_id), summary)
    end
end

return M
