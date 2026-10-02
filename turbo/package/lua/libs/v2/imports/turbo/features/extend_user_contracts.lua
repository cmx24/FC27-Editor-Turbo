-- Turbo feature: extend contracts of your club's players to N years from today.
-- Port of FC 26 extend_user_team_players_contracts.lua.
-- FC 26 used the career_playercontract table; FC 27 Live Editor stopped reading it, so Turbo
-- uses it only when it exists with the needed fields, and always updates players.contractvaliduntil.
--   "extend_user_contracts": { "years": 4 }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'

local M = {}

local CONTRACT_FIELDS = { "playerid", "contract_status", "contract_date", "last_status_change_date", "duration_months" }
local LOANED_IN = { [1] = true, [3] = true, [5] = true }   -- contract_status values FC 26 treated as loaned-in

function M.run(ctx)
    local years = util.to_int(ctx.cfg.years)
    if not years or years < 1 or years > 10 then return false, "years must be an integer 1..10" end

    local today = game.current_date()
    if not today then return false, "current in-game date not available" end

    local squad, count, source = game.user_squad()
    if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end

    local players, err = db.get_table("players")
    if not players then return false, err end
    local ok, missing = db.has_fields(players, { "playerid", "contractvaliduntil" })
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    local new_until = today.year + years
    local cv, cverr = db.validate(players, "contractvaliduntil", new_until)
    if cv == nil then return false, cverr end

    -- Optional career_playercontract pass
    local skip_loaned = {}
    local contract_rows = 0
    local contracts = db.get_table("career_playercontract")
    local use_contracts = contracts and db.has_fields(contracts, CONTRACT_FIELDS)
    if use_contracts then
        local months = years * 12
        local mv, merr = db.validate(contracts, "duration_months", months)
        if mv == nil then return false, merr end
        for rec in db.records(contracts) do
            local pid = contracts:GetRecordFieldValue(rec, "playerid")
            if squad[pid] then
                local status = contracts:GetRecordFieldValue(rec, "contract_status")
                if LOANED_IN[status] then
                    skip_loaned[pid] = true
                else
                    for _, f in ipairs({ "contract_date", "last_status_change_date" }) do
                        local cur = contracts:GetRecordFieldValue(rec, f)
                        if cur and cur < today.int then
                            local wok, werr = db.set(contracts, rec, f, today.int, ctx.dry)
                            if not wok then return false, werr end
                        end
                    end
                    local wok, werr = db.set(contracts, rec, "duration_months", months, ctx.dry)
                    if not wok then return false, werr end
                    contract_rows = contract_rows + 1
                end
            end
        end
    end

    local updated = 0
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if squad[pid] and not skip_loaned[pid] then
            local wok, werr = db.set(players, rec, "contractvaliduntil", new_until, ctx.dry)
            if not wok then return false, string.format("player %d: %s", pid, werr) end
            updated = updated + 1
        end
    end

    local summary = string.format("%d of %d squad players now contracted until %d", updated, count, new_until)
    if use_contracts then
        summary = summary .. string.format("; career_playercontract: %d rows set to %d months", contract_rows, years * 12)
    else
        summary = summary .. "; career_playercontract not present, players table only"
    end
    if util.count(skip_loaned) > 0 then summary = summary .. string.format("; %d loaned-in players skipped", util.count(skip_loaned)) end
    return true, summary
end

return M
