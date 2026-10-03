-- Turbo feature: current-season player stats to CSV.
-- Port of FC 26 export_season_stats.lua. Names are looked up only for players that have stats
-- (the FC 26 script looked up every player in the database), and CSV cells are quoted.
-- FC 27 Live Editor v27.1.2 has no GetPlayersStats: then the league columns the database keeps per player and club
-- (teamplayerlinks: leagueappearances, leaguegoals, yellows, reds) are exported instead, one line per club link.
-- Seen in game (02-10-2026, two league matches into a career): leagueappearances stays 0 and the goals include national
-- team links, so these are the database's stored numbers, not live season statistics; the file name and the message
-- say so.
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

local function position_map()
    local positions = {}
    local players = db.get_table("players")
    if players and db.has_fields(players, { "playerid", "preferredposition1" }) then
        for rec in db.records(players) do
            positions[players:GetRecordFieldValue(rec, "playerid")] = players:GetRecordFieldValue(rec, "preferredposition1")
        end
    end
    return positions
end

local function sort_rows(rows)
    table.sort(rows, function(a, b)
        if a.goals ~= b.goals then return a.goals > b.goals end
        if a.playerid ~= b.playerid then return a.playerid < b.playerid end
        return tostring(a.competition) < tostring(b.competition)
    end)
end

-- League stats from the database (teamplayerlinks), used when Live Editor has no GetPlayersStats
local function rows_from_links(only_user, squad, user_team)
    local links, err = db.get_table("teamplayerlinks")
    if not links then return nil, err end
    if not db.has_fields(links, { "playerid", "teamid", "leaguegoals" }) then
        return nil, "teamplayerlinks lacks playerid/teamid/leaguegoals"
    end
    local has = function(f) return db.has_field(links, f) end
    local positions = position_map()
    local rows, names, teams = {}, {}, {}
    for rec in db.records(links) do
        local pid = n(links:GetRecordFieldValue(rec, "playerid"))
        local tid = n(links:GetRecordFieldValue(rec, "teamid"))
        local app = has("leagueappearances") and n(links:GetRecordFieldValue(rec, "leagueappearances")) or 0
        local goals = n(links:GetRecordFieldValue(rec, "leaguegoals"))
        local yellow = has("yellows") and n(links:GetRecordFieldValue(rec, "yellows")) or 0
        local red = has("reds") and n(links:GetRecordFieldValue(rec, "reds")) or 0
        local wanted = not only_user or (squad[pid] and (user_team <= 0 or tid == user_team))
        if pid > 0 and wanted and (app > 0 or goals > 0 or yellow > 0 or red > 0) then
            names[pid] = names[pid] or game.player_name(pid)
            teams[tid] = teams[tid] or game.team_name(tid)
            rows[#rows + 1] = {
                position = POS[positions[pid]] or "", playerid = pid, playername = names[pid], team = teams[tid],
                competition = "database (teamplayerlinks)", appearances = app, AVG = "", MOTMs = "", goals = goals,
                assists = "", yellow_cards = yellow, two_yellow = "", red_cards = red, saves = "",
                goals_conceded = "", cleansheets = "",
            }
        end
    end
    return rows, names
end

function M.run(ctx)
    if not ctx.out_dir then return false, "no writable output folder" end

    local only_user = ctx.cfg.only_user_team == true
    local squad = {}
    if only_user then
        local count, source
        squad, count, source = game.user_squad()
        if count == 0 then return false, "user squad not found (" .. tostring(source) .. ")" end
    end

    local today = game.current_date()
    local path = util.join(ctx.out_dir, "turbo_season_stats_" .. util.timestamp_suffix(today) .. ".csv")

    if type(GetPlayersStats) ~= "function" then
        local rows, names = rows_from_links(only_user, squad, only_user and game.user_team_id() or 0)
        if not rows then return false, "GetPlayersStats is not available in this Live Editor build, and " .. tostring(names) end
        sort_rows(rows)
        local dbpath = util.join(ctx.out_dir, "turbo_database_league_stats_" .. util.timestamp_suffix(today) .. ".csv")
        if not ctx.dry then
            local wok, werr = csv.write(dbpath, COLUMNS, rows)
            if not wok then return false, "cannot write " .. dbpath .. ": " .. tostring(werr) end
        end
        return true, string.format("%d lines (%d players) saved to %s. This Live Editor build has no GetPlayersStats, so "
            .. "these are the league goals and cards the database stores per club link (teamplayerlinks), not live "
            .. "season statistics", #rows, util.count(names), dbpath)
    end

    local ok, stats = pcall(GetPlayersStats)
    if not ok or not util.is_object(stats) then return false, "GetPlayersStats failed: " .. tostring(stats) end

    -- playerid -> primary position (one pass over players)
    local positions = position_map()

    local rows, names, teams, comps = {}, {}, {}, {}
    for i = 1, util.len(stats) do
        local s = util.index(stats, i)
        local function f(k) return util.index(s, k) end
        local pid = n(f("playerid"))
        local app = n(f("app"))
        if pid > 0 and pid < 4294967295 and app > 0 and (not only_user or squad[pid]) then
            names[pid] = names[pid] or game.player_name(pid)
            local tid = n(f("teamid"))
            if tid <= 0 then tid = game.team_of_player(pid) end
            teams[tid] = teams[tid] or game.team_name(tid)
            local cid = n(f("compobjid"))
            local compname = f("compname")
            if type(compname) ~= "string" or compname == "" then
                comps[cid] = comps[cid] or game.competition_name(cid)
                compname = comps[cid]
            end
            local avg = n(f("avg")) / app / 10
            rows[#rows + 1] = {
                position = POS[positions[pid]] or "",
                playerid = pid,
                playername = names[pid],
                team = teams[tid],
                competition = compname,
                appearances = app,
                AVG = string.format("%.2f", avg),
                MOTMs = n(f("motm")),
                goals = n(f("goals")),
                assists = n(f("assists")),
                yellow_cards = n(f("yellow")),
                two_yellow = n(f("two_yellow")),
                red_cards = n(f("red")),
                saves = n(f("saves")),
                goals_conceded = n(f("goals_conceded")),
                cleansheets = n(f("clean_sheets")),
            }
        end
    end

    sort_rows(rows)
    if not ctx.dry then
        local wok, werr = csv.write(path, COLUMNS, rows)
        if not wok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("%d stat lines (%d players) saved to %s", #rows, util.count(names), path)
end

return M
