package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t20 national teams: the game's rule (league 78 / 2136 / 3004), not every teamnationlinks row (Rest of World clubs)")

local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })
-- FC 27's teamnationlinks ties national teams AND some clubs to a nation: in game 2026-10-05, 85 of its 154 rows were clubs
-- (Dinamo Zagreb 211 with league 76 = Rest of World); team 9 plays that part here, teams 8 / 7 are national teams
sim:add_table({ name = "teamnationlinks", short = "tnl_",
    fields = { { name = "nationid", short = "nid_", depth = 12 }, { name = "teamid", short = "tid_", depth = 18 },
               { name = "leagueid", short = "lid_", depth = 12 } },
    rows = { { nationid = 10, teamid = 9, leagueid = 76 }, { nationid = 14, teamid = 8, leagueid = 78 },
             { nationid = 15, teamid = 7, leagueid = 2136 } } })

local moves = require 'imports/turbo/core/moves'
moves.reset_cache()

local function link_team(pid)
    local rec = sim:find_row("teamplayerlinks", "playerid", pid)
    return rec and sim:value("teamplayerlinks", rec, "teamid") or nil
end
local function player_of(team)
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == team then return sim:value("teamplayerlinks", rec, "playerid") end
    end
end
local function moves_run(actions) return H.turbo().run("player_moves", { actions = actions }) end

H.case("the national-team set is the international leagues only", function()
    local nat = moves.national_teams()
    H.eq(nat[8], true, "league 78"); H.eq(nat[7], true, "league 2136")
    H.eq(nat[9], nil, "a Rest of World club (league 76) is a club")
end)

H.case("a Rest of World club's player has a club link and can be transferred (he could not before)", function()
    local pid = player_of(9)
    H.ok(pid, "a player of team 9")
    local rec, tid = moves.club_link(pid)
    H.ok(rec, "club link found"); H.eq(tid, 9)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 3, months = 60 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 3)
end)

H.case("a transfer to a Rest of World club is allowed, to a national team refused", function()
    local pid = player_of(4)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 9, months = 60 } })
    H.eq(ok, true, msg); H.eq(link_team(pid), 9)
    ok, msg = moves_run({ { action = "transfer", playerid = player_of(5), to_teamid = 8, months = 60 } })
    H.eq(ok, false); H.has(msg, "national team")
    ok, msg = moves_run({ { action = "transfer", playerid = player_of(5), to_teamid = 7, months = 60 } })
    H.eq(ok, false); H.has(msg, "national team")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
