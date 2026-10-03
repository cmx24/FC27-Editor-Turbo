-- Turbo feature: assign tattoos to players.
-- Port of FC 26 custom_tattoos_to_playerid.lua (which crashed: it counted an undefined table).
--   "custom_tattoos": { "field": "tattooleftarm", "map": { "201942": 70 } }   (playerid -> tattoo id)

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'

local M = {}

function M.run(ctx)
    local field = ctx.cfg.field or "tattooleftarm"
    if type(field) ~= "string" or not field:match("^tattoo") then
        return false, "field must be a players-table tattoo field (e.g. tattooleftarm), got " .. tostring(field)
    end
    local map, bad = util.int_key_map(ctx.cfg.map)
    if #bad > 0 then return false, "map has non-integer entries: " .. table.concat(bad, ", ") end
    local wanted = util.count(map)
    if wanted == 0 then return false, "map is empty: add \"playerid\": tattooid pairs in turbo_config.json" end

    local players, err = db.get_table("players")
    if not players then return false, err end
    local ok, missing = db.has_fields(players, { "playerid", field })
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    for pid, tattoo in pairs(map) do
        local v, verr = db.validate(players, field, tattoo)
        if v == nil then return false, string.format("player %d: %s", pid, verr) end
    end

    local done = {}
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        local tattoo = map[pid]
        if tattoo and not done[pid] then
            local wok, werr = db.set(players, rec, field, tattoo, ctx.dry)
            if not wok then return false, string.format("player %d: %s", pid, werr) end
            done[pid] = true
            if util.count(done) == wanted then break end
        end
    end

    local not_found = {}
    for pid in pairs(map) do
        if not done[pid] then not_found[#not_found + 1] = tostring(pid) end
    end
    local summary = string.format("%s updated for %d of %d players", field, util.count(done), wanted)
    if #not_found > 0 then summary = summary .. "; not found: " .. table.concat(not_found, ", ") end
    return true, summary
end

return M
