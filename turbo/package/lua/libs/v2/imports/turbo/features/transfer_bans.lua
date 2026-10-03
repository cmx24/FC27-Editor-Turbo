-- Turbo feature: transfer bans.
-- Ports FC 26 list_transfer_bans.lua (which labelled every ban as a team ban because it compared
-- against a constant that is local to the library) and transfer_ban_all_teams.lua.
--   "transfer_bans": { "mode": "list" | "ban_all_teams" | "unban_all_teams", "ban_until": 20990101,
--                      "exclude_user_team": false }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local csv = require 'imports/turbo/core/csv'
local log = require 'imports/turbo/core/log'

local M = {}

local TYPE_TEAM = 0
local TYPE_PLAYER = 1

local function need(names)
    for _, n in ipairs(names) do
        if type(_G[n]) ~= "function" then return false, n .. " is not available in this Live Editor build" end
    end
    return true
end

local function all_team_ids()
    local teams, err = db.get_table("teams")
    if not teams then return nil, err end
    if not db.has_field(teams, "teamid") then return nil, "teams table lacks teamid" end
    local ids = {}
    for rec in db.records(teams) do
        local tid = teams:GetRecordFieldValue(rec, "teamid")
        if tid and tid > 0 then ids[#ids + 1] = tid end
    end
    return ids
end

local function list(ctx)
    local ok, err = need({ "cGetTransferBans" })
    if not ok then return false, err end
    local bans = cGetTransferBans() or {}
    local rows = {}
    for i = 1, #bans do
        local b = bans[i]
        local id = b.id or b.member_id
        local kind = (b.member_type == TYPE_PLAYER) and "player" or "team"
        local name = kind == "player" and game.player_name(id) or game.team_name(id)
        rows[#rows + 1] = { type = kind, id = id, name = name, banned_until = b.date_end }
        log.info("%s %s (ID %s) banned until %s", kind, tostring(name), tostring(id), tostring(b.date_end))
    end
    local summary = string.format("%d active transfer bans", #rows)
    if ctx.out_dir and #rows > 0 then
        local path = util.join(ctx.out_dir, "turbo_transfer_bans.csv")
        local wok = csv.write(path, { "type", "id", "name", "banned_until" }, rows)
        if wok then summary = summary .. "; saved to " .. path end
    end
    return true, summary
end

local function ban_all(ctx)
    local ok, err = need({ "cAddTransferBan", "cSaveTransferBans" })
    if not ok then return false, err end
    local until_date = util.to_int(ctx.cfg.ban_until)
    if not until_date or not util.is_yyyymmdd(until_date) then return false, "ban_until must be a date as YYYYMMDD" end
    local ids, terr = all_team_ids()
    if not ids then return false, terr end
    local skip = ctx.cfg.exclude_user_team == true and game.user_team_id() or -1
    local n = 0
    for _, tid in ipairs(ids) do
        if tid ~= skip then
            if not ctx.dry then cAddTransferBan(tid, until_date, TYPE_TEAM) end
            n = n + 1
        end
    end
    if not ctx.dry then cSaveTransferBans() end
    return true, string.format("%d teams transfer-banned until %d%s", n, until_date,
        skip > 0 and " (your club excluded)" or "")
end

local function unban_all(ctx)
    local ok, err = need({ "cGetTransferBans", "cRemoveTransferBan", "cSaveTransferBans" })
    if not ok then return false, err end
    local bans = cGetTransferBans() or {}
    local n = 0
    for i = 1, #bans do
        local b = bans[i]
        if b.member_type ~= TYPE_PLAYER then
            if not ctx.dry then cRemoveTransferBan(b.id or b.member_id, TYPE_TEAM) end
            n = n + 1
        end
    end
    if not ctx.dry then cSaveTransferBans() end
    return true, string.format("%d team bans removed", n)
end

function M.run(ctx)
    local mode = ctx.cfg.mode
    if mode == "list" then return list(ctx) end
    if mode == "ban_all_teams" then return ban_all(ctx) end
    if mode == "unban_all_teams" then return unban_all(ctx) end
    return false, "mode must be \"list\", \"ban_all_teams\" or \"unban_all_teams\""
end

return M
