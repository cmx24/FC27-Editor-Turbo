-- Turbo feature: delete generated players (playerid >= min_playerid).
-- Port of FC 26 delete_generated_players.lua. Destructive, so it only lists by default:
--   "delete_generated_players": { "min_playerid": 460000, "confirm": true }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'
local env = require 'imports/turbo/core/env'
local moves = require 'imports/turbo/core/moves'

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
    -- Live Editor's DeletePlayer when it exists, else Turbo's own delete (core/moves.lua, same steps)
    local delete_player = env.api("DeletePlayer")
    if not delete_player then
        delete_player = function(pid)
            local okd, derr = moves.delete(pid, false)
            if not okd then error(derr) end
        end
    end
    -- Turbo's own delete never touches your club (core/moves.lua guard): those players are left alone and counted
    local skipped = 0
    if not env.api("DeletePlayer") then
        local user = moves.user_team()
        local keep = {}
        for _, pid in ipairs(ids) do
            local _, tid = moves.club_link(pid)
            local lrec, loans = moves.loan_row(pid)
            local owner = lrec and loans:GetRecordFieldValue(lrec, "teamidloanedfrom") or nil
            if user <= 0 or tid == user or owner == user then skipped = skipped + 1 else keep[#keep + 1] = pid end
        end
        ids = keep
    end
    local skip_note = skipped > 0 and string.format("; %d in your club are kept (Turbo never deletes your players)", skipped) or ""
    if ctx.cfg.confirm ~= true or ctx.dry then
        return true, string.format("%d generated players found (playerid >= %d)%s. Set \"confirm\": true to delete them.",
            #ids, min_pid, skip_note)
    end

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
    return failed == 0, string.format("deleted %d generated players%s%s", deleted,
        failed > 0 and string.format(", %d failed", failed) or "", skip_note)
end

return M
