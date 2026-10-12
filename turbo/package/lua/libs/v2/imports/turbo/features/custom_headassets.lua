-- Turbo feature: assign head models to players.
-- Port of FC 26 custom_headassetid_to_playerid.lua. The map lives in turbo_config.json:
--   "custom_headassets": { "map": { "158023": 20801 } }   (playerid -> headassetid)
-- Asset gate (core/asset_gate.lua, 2.1): while the Turbo GUI's asset catalog matches the game (lua_gate.txt of this
-- session), a head the game files do not ship is refused before anything is written, and so is a head no player of
-- the save uses when turbo_output\asset_hidden_off.txt exists. Without a current gate the run is logged "unchecked"
-- and today's rules apply (range check only). Every head written is appended to turbo_output\asset_writes.log.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'
local agate = require 'imports/turbo/core/asset_gate'

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
    local gate = agate.load()
    local heads = agate.db_heads()
    local pids = {}
    for pid in pairs(map) do pids[#pids + 1] = pid end
    table.sort(pids)
    for _, pid in ipairs(pids) do
        local okg, why = agate.check_head(gate, map[pid], heads, false)
        if not okg then
            log.warn("custom_headassets refused (player %d, head %d): %s", pid, map[pid], why)
            return false, string.format("player %d: %s; nothing written", pid, why)
        end
    end
    if not agate.trusted(gate) then log.info("custom_headassets: heads unchecked (%s)", tostring(gate.reason)) end

    local done, logged = {}, 0
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        local asset = map[pid]
        if asset and not done[pid] then
            for _, w in ipairs({ { "hashighqualityhead", 1 }, { "headclasscode", 0 }, { "headassetid", asset } }) do
                local old = players:GetRecordFieldValue(rec, w[1])
                local wok, werr = db.set(players, rec, w[1], w[2], ctx.dry)
                if not wok then return false, string.format("player %d: %s", pid, werr) end
                if not ctx.dry and old ~= w[2] and agate.log_write(gate, "player " .. pid, w[1], old, w[2]) then
                    logged = logged + 1
                end
            end
            done[pid] = true
            if util.count(done) == wanted then break end
        end
    end

    local not_found = {}
    for pid in pairs(map) do
        if not done[pid] then not_found[#not_found + 1] = tostring(pid) end
    end
    table.sort(not_found)
    local summary = string.format("updated %d of %d players", util.count(done), wanted)
    if #not_found > 0 then summary = summary .. "; not found: " .. table.concat(not_found, ", ") end
    if agate.trusted(gate) then
        summary = summary .. "; heads " .. gate.reason
    else
        summary = summary .. "; heads unchecked (" .. tostring(gate.reason) .. ")"
    end
    if logged > 0 then summary = summary .. string.format("; %d change(s) logged in turbo_output\\%s", logged, agate.WRITE_LOG) end
    return true, summary
end

return M
