-- Turbo feature: extend contracts of every player outside your club by N years.
-- Port of FC 26 extend_cpu_players_contracts.lua (that script read and wrote back the same
-- value, so it never extended anything). Capped at the field's maximum.
--   "extend_cpu_contracts": { "years": 5 }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'

local M = {}

function M.run(ctx)
    local years = util.to_int(ctx.cfg.years)
    if not years or years < 1 or years > 20 then return false, "years must be an integer 1..20" end

    local players, err = db.get_table("players")
    if not players then return false, err end
    local ok, missing = db.has_fields(players, { "playerid", "contractvaliduntil" })
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    local exclude, n_excluded = {}, 0
    if game.in_cm() then
        exclude, n_excluded = game.user_squad()
    end

    local info = db.field_info(players, "contractvaliduntil")
    local changed, capped, skipped = 0, 0, 0
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if not exclude[pid] then
            local cur = players:GetRecordFieldValue(rec, "contractvaliduntil")
            if cur and cur > info.min then
                local new = cur + years
                if info.max and new > info.max then
                    new = info.max
                    capped = capped + 1
                end
                if new ~= cur then
                    local wok, werr = db.set(players, rec, "contractvaliduntil", new, ctx.dry)
                    if not wok then return false, string.format("player %d: %s", pid, werr) end
                    changed = changed + 1
                end
            else
                skipped = skipped + 1
            end
        end
    end
    return true, string.format("extended %d contracts by %d years (%d hit the field maximum, %d without contract skipped, %d of your players excluded)",
        changed, years, capped, skipped, n_excluded)
end

return M
