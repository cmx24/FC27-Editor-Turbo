-- Turbo feature: assign head models to players.
-- Port of FC 26 custom_headassetid_to_playerid.lua. The map lives in turbo_config.json:
--   "custom_headassets": { "map": { "158023": 20801 } }   (playerid -> headassetid)

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'

local M = {}

local FIELDS = { "playerid", "hashighqualityhead", "headclasscode", "headassetid" }

function M.run(ctx)
    local map, bad = util.int_key_map(ctx.cfg.map)
    if #bad > 0 then return false, "map has non-integer entries: " .. table.concat(bad, ", ") end
    local wanted = util.count(map)
    if wanted == 0 then return false, "map is empty: add \"playerid\": headassetid pairs in turbo_config.json" end

    local players, err = db.get_table("players")
    if not players then return false, err end
    local ok, missing = db.has_fields(players, FIELDS)
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    -- Validate every value before touching anything
    for pid, asset in pairs(map) do
        local v, verr = db.validate(players, "headassetid", asset)
        if v == nil then return false, string.format("player %d: %s", pid, verr) end
    end

    local done = {}
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        local asset = map[pid]
        if asset and not done[pid] then
            for _, w in ipairs({ { "hashighqualityhead", 1 }, { "headclasscode", 0 }, { "headassetid", asset } }) do
                local wok, werr = db.set(players, rec, w[1], w[2], ctx.dry)
                if not wok then return false, string.format("player %d: %s", pid, werr) end
            end
            done[pid] = true
            if util.count(done) == wanted then break end
        end
    end

    local not_found = {}
    for pid in pairs(map) do
        if not done[pid] then not_found[#not_found + 1] = tostring(pid) end
    end
    local summary = string.format("updated %d of %d players", util.count(done), wanted)
    if #not_found > 0 then summary = summary .. "; not found: " .. table.concat(not_found, ", ") end
    return true, summary
end

return M
