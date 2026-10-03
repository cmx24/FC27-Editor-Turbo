-- Turbo feature: list the kit numbers used by a team.
-- Port of FC 26 print_team_jersey_numbers.lua; also writes a CSV.
--   "team_jersey_numbers": { "teamid": 0 }   (0 = your club in career mode)

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local csv = require 'imports/turbo/core/csv'
local log = require 'imports/turbo/core/log'

local M = {}

function M.run(ctx)
    local teamid = util.to_int(ctx.cfg.teamid) or 0
    if teamid == 0 then teamid = game.user_team_id() end
    if teamid <= 0 then return false, "set teamid in turbo_config.json (or load a career to use your club)" end

    local links, err = db.get_table("teamplayerlinks")
    if not links then return false, err end
    local ok, missing = db.has_fields(links, { "teamid", "playerid", "jerseynumber" })
    if not ok then return false, "teamplayerlinks lacks fields: " .. table.concat(missing, ", ") end

    local rows = {}
    for rec in db.records(links) do
        if links:GetRecordFieldValue(rec, "teamid") == teamid then
            local pid = links:GetRecordFieldValue(rec, "playerid")
            rows[#rows + 1] = { number = links:GetRecordFieldValue(rec, "jerseynumber"), playerid = pid, player = game.player_name(pid) }
        end
    end
    if #rows == 0 then return false, string.format("no players linked to team %d", teamid) end
    table.sort(rows, function(a, b) return (a.number or 0) < (b.number or 0) end)

    local used, dupes = {}, {}
    for _, r in ipairs(rows) do
        log.info("Number: %s, Player: %s (%s)", tostring(r.number), r.player, tostring(r.playerid))
        local key = r.number or -1
        if used[key] then dupes[#dupes + 1] = tostring(r.number) end
        used[key] = true
    end

    local summary = string.format("%s (ID %d): %d players", game.team_name(teamid), teamid, #rows)
    if #dupes > 0 then summary = summary .. "; duplicate numbers: " .. table.concat(dupes, ", ") end
    if ctx.out_dir then
        local path = util.join(ctx.out_dir, string.format("turbo_jersey_numbers_%d.csv", teamid))
        if csv.write(path, { "number", "playerid", "player" }, rows) then summary = summary .. "; saved to " .. path end
    end
    return true, summary
end

return M
