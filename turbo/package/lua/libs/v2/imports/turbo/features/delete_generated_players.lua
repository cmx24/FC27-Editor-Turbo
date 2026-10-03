-- Turbo feature: delete generated players (playerid >= min_playerid).
-- Port of FC 26 delete_generated_players.lua. Destructive, so it only lists by default:
--   "delete_generated_players": { "min_playerid": 460000, "confirm": true }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'
local env = require 'imports/turbo/core/env'

local M = {}

function M.run(ctx)
    local min_pid = util.to_int(ctx.cfg.min_playerid)
    if not min_pid or min_pid < 300000 then
        return false, "min_playerid must be an integer >= 300000 (generated players start at 460000 in FC 26)"
    end

    local players, err = db.get_table("players")
    if not players then return false, err end
    if not db.has_field(players, "playerid") then return false, "players table lacks playerid" end

    -- Collect first, delete after: deleting changes the table while it is being walked
    local ids = {}
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if pid and pid >= min_pid then ids[#ids + 1] = pid end
    end
    table.sort(ids)

    if #ids == 0 then return true, string.format("no players with playerid >= %d", min_pid) end
    -- Counting works with any Live Editor build; deleting needs Live Editor's DeletePlayer
    local delete_player, why = env.api("DeletePlayer")
    if ctx.cfg.confirm ~= true or ctx.dry then
        return true, string.format("%d generated players found (playerid >= %d). %s", #ids, min_pid,
            delete_player and "Set \"confirm\": true to delete them." or ("Deleting them is not possible here: " .. why))
    end
    if not delete_player then return false, why end

    local deleted, failed = 0, 0
    for _, pid in ipairs(ids) do
        local ok, derr = pcall(delete_player, pid, 0)
        if ok then
            deleted = deleted + 1
        else
            failed = failed + 1
            log.warn("DeletePlayer(%d) failed: %s", pid, tostring(derr))
        end
    end
    return failed == 0, string.format("deleted %d generated players%s", deleted,
        failed > 0 and string.format(", %d failed", failed) or "")
end

return M
