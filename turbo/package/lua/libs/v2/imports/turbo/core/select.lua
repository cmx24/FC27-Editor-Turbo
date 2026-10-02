-- FC 27 LE Turbo - player selection shared by bulk_edit and player_moves
-- scope   : { "user_team": true } | { "teamids": [1, 2] } | { "playerids": [158023] } | { "all": true }
-- filters : { "min_overall", "max_overall", "min_potential", "max_potential", "min_age", "max_age",
--             "positions": [0..27] (preferredposition1), "max_playerid", "min_playerid" }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'

local M = {}

local FILTER_KEYS = {
    min_overall = true, max_overall = true, min_potential = true, max_potential = true,
    min_age = true, max_age = true, positions = true, min_playerid = true, max_playerid = true,
}

local function team_members(teamids)
    local wanted = util.set_of(teamids)
    local out = {}
    local links, err = db.get_table("teamplayerlinks")
    if not links then return nil, err end
    if not db.has_fields(links, { "teamid", "playerid" }) then return nil, "teamplayerlinks lacks teamid/playerid" end
    for rec in db.records(links) do
        if wanted[links:GetRecordFieldValue(rec, "teamid")] then
            out[links:GetRecordFieldValue(rec, "playerid")] = true
        end
    end
    return out
end

-- Returns list of {rec, pid}, players_table, description  or nil, error
function M.players(scope, filters, opts)
    opts = opts or {}
    scope = type(scope) == "table" and scope or {}
    filters = type(filters) == "table" and filters or {}

    for k in pairs(filters) do
        if not FILTER_KEYS[k] then return nil, "unknown filter: " .. tostring(k) end
    end

    local players, err = db.get_table("players")
    if not players then return nil, err end
    if not db.has_field(players, "playerid") then return nil, "players table lacks playerid" end

    -- Scope -> allowed set (nil = all players)
    local allowed, desc
    if scope.user_team == true then
        local count, source
        allowed, count, source = game.user_squad()
        if count == 0 then return nil, "user squad not found (" .. tostring(source) .. ")" end
        desc = "your squad"
    elseif type(scope.teamids) == "table" then
        local ids, bad = util.int_list(scope.teamids)
        if bad > 0 or #ids == 0 then return nil, "scope.teamids must be a non-empty list of integers" end
        local terr
        allowed, terr = team_members(ids)
        if not allowed then return nil, terr end
        desc = "teams " .. table.concat(ids, ",")
    elseif type(scope.playerids) == "table" then
        local ids, bad = util.int_list(scope.playerids)
        if bad > 0 or #ids == 0 then return nil, "scope.playerids must be a non-empty list of integers" end
        allowed = util.set_of(ids)
        desc = #ids .. " listed players"
    elseif scope.all == true then
        if not opts.allow_all then return nil, "scope.all needs \"confirm_all\": true" end
        desc = "all players"
    else
        return nil, "scope must set one of user_team, teamids, playerids, all"
    end

    -- Filter prerequisites
    local function need(field)
        if not db.has_field(players, field) then return false end
        return true
    end
    local f = {}
    for _, k in ipairs({ "min_overall", "max_overall", "min_potential", "max_potential", "min_age", "max_age", "min_playerid", "max_playerid" }) do
        if filters[k] ~= nil then
            f[k] = util.to_int(filters[k])
            if f[k] == nil then return nil, "filter " .. k .. " must be an integer" end
        end
    end
    if (f.min_overall or f.max_overall) and not need("overallrating") then return nil, "players table lacks overallrating" end
    if (f.min_potential or f.max_potential) and not need("potential") then return nil, "players table lacks potential" end
    local positions = nil
    if filters.positions ~= nil then
        local list, bad = util.int_list(filters.positions)
        if bad > 0 or #list == 0 then return nil, "filter positions must be a list of position ids 0..27" end
        positions = util.set_of(list)
        if not need("preferredposition1") then return nil, "players table lacks preferredposition1" end
    end
    local today, by_age = nil, false
    if f.min_age or f.max_age then
        if not need("birthdate") then return nil, "players table lacks birthdate" end
        today = game.current_date()
        if not today then return nil, "age filter needs the in-game date" end
        by_age = true
    end

    local out = {}
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        local keep = pid ~= nil and (allowed == nil or allowed[pid] == true)
        if keep and f.min_playerid and pid < f.min_playerid then keep = false end
        if keep and f.max_playerid and pid > f.max_playerid then keep = false end
        if keep and (f.min_overall or f.max_overall) then
            local ovr = players:GetRecordFieldValue(rec, "overallrating")
            if (f.min_overall and ovr < f.min_overall) or (f.max_overall and ovr > f.max_overall) then keep = false end
        end
        if keep and (f.min_potential or f.max_potential) then
            local pot = players:GetRecordFieldValue(rec, "potential")
            if (f.min_potential and pot < f.min_potential) or (f.max_potential and pot > f.max_potential) then keep = false end
        end
        if keep and positions then
            if not positions[players:GetRecordFieldValue(rec, "preferredposition1")] then keep = false end
        end
        if keep and by_age then
            local by, bm, bdd = util.date_from_gregorian_days(players:GetRecordFieldValue(rec, "birthdate"))
            local age = today.year - by
            if today.month < bm or (today.month == bm and today.day < bdd) then age = age - 1 end
            if (f.min_age and age < f.min_age) or (f.max_age and age > f.max_age) then keep = false end
        end
        if keep then out[#out + 1] = { rec = rec, pid = pid } end
    end
    return out, players, desc
end

return M
