-- Turbo feature: current-season player stats to CSV.
-- Port of FC 26 export_season_stats.lua. Names are looked up only for players that have stats
-- (the FC 26 script looked up every player in the database), and CSV cells are quoted.
--   "export_season_stats": { "only_user_team": false }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local csv = require 'imports/turbo/core/csv'

local M = {}

local COLUMNS = {
    "position", "playerid", "playername", "team", "competition", "appearances", "AVG", "MOTMs",
    "goals", "assists", "yellow_cards", "two_yellow", "red_cards", "saves", "goals_conceded", "cleansheets",
}

local POS = {
    [0] = "GK", [1] = "SW", [2] = "RWB", [3] = "RB", [4] = "RCB", [5] = "CB", [6] = "LCB", [7] = "LB",
    [8] = "LWB", [9] = "RDM", [10] = "CDM", [11] = "LDM", [12] = "RM", [13] = "RCM", [14] = "CM",
    [15] = "LCM", [16] = "LM", [17] = "RAM", [18] = "CAM", [19] = "LAM", [20] = "RF", [21] = "CF",
    [22] = "LF", [23] = "RW", [24] = "RS", [25] = "ST", [26] = "LS", [27] = "LW",
}

local function n(v) return util.to_int(v) or 0 end

function M.run(ctx)
    if type(GetPlayersStats) ~= "function" then return false, "GetPlayersStats is not available in this Live Editor build" end
    if not ctx.out_dir then return false, "no writable output folder" end

    local ok, stats = pcall(GetPlayersStats)
    if not ok or type(stats) ~= "table" then return false, "GetPlayersStats failed: " .. tostring(stats) end

    local only_user = ctx.cfg.only_user_team == true
    local squad = {}
    if only_user then
        local count, source
        squad, count, source = game.user_squad()
        if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end
    end

    -- playerid -> primary position (one pass over players)
    local positions = {}
    local players = db.get_table("players")
    if players and db.has_fields(players, { "playerid", "preferredposition1" }) then
        for rec in db.records(players) do
            positions[players:GetRecordFieldValue(rec, "playerid")] = players:GetRecordFieldValue(rec, "preferredposition1")
        end
    end

    local rows, names, teams, comps = {}, {}, {}, {}
    for i = 1, #stats do
        local s = stats[i]
        local pid = n(s.playerid)
        local app = n(s.app)
        if pid > 0 and pid < 4294967295 and app > 0 and (not only_user or squad[pid]) then
            names[pid] = names[pid] or game.player_name(pid)
            local tid = n(s.teamid)
            if tid <= 0 then tid = game.team_of_player(pid) end
            teams[tid] = teams[tid] or game.team_name(tid)
            local cid = n(s.compobjid)
            local compname = s.compname
            if type(compname) ~= "string" or compname == "" then
                comps[cid] = comps[cid] or game.competition_name(cid)
                compname = comps[cid]
            end
            local avg = n(s.avg) / app / 10
            rows[#rows + 1] = {
                position = POS[positions[pid]] or "",
                playerid = pid,
                playername = names[pid],
                team = teams[tid],
                competition = compname,
                appearances = app,
                AVG = string.format("%.2f", avg),
                MOTMs = n(s.motm),
                goals = n(s.goals),
                assists = n(s.assists),
                yellow_cards = n(s.yellow),
                two_yellow = n(s.two_yellow),
                red_cards = n(s.red),
                saves = n(s.saves),
                goals_conceded = n(s.goals_conceded),
                cleansheets = n(s.clean_sheets),
            }
        end
    end

    table.sort(rows, function(a, b)
        if a.goals ~= b.goals then return a.goals > b.goals end
        if a.playerid ~= b.playerid then return a.playerid < b.playerid end
        return tostring(a.competition) < tostring(b.competition)
    end)

    local today = game.current_date()
    local path = util.join(ctx.out_dir, "turbo_season_stats_" .. util.timestamp_suffix(today) .. ".csv")
    if not ctx.dry then
        local wok, werr = csv.write(path, COLUMNS, rows)
        if not wok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("%d stat lines (%d players) saved to %s", #rows, util.count(names), path)
end

return M
