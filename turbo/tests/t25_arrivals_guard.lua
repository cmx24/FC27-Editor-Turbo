package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t25 arrivals guard: no raw database move into your club without the opt-in, honest development plan words, squad status entry at create")

-- FC 27 LE v27.1.2 (no move natives: Turbo's own moves), the FC 27 squad status table, development attributes, one loaned-in player
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, development = true, fc27_status = true })

local USER = W.USER_TEAM
local OPT_IN = "db_move_user_club_on.txt"

local function pval(pid, field)
    local rec = sim:find_row("players", "playerid", pid)
    return rec and sim:value("players", rec, field) or nil
end
local function pset(pid, field, v)
    local rec = sim:find_row("players", "playerid", pid)
    sim:set_field(rec, sim.tables.players[field], v)
end
local function link_team(pid)
    local rec = sim:find_row("teamplayerlinks", "playerid", pid)
    return rec and sim:value("teamplayerlinks", rec, "teamid") or nil
end
local function db_bytes()
    local snap = {}
    for _, t in pairs(sim.tables) do
        for a = t.first, t.first + t.rec_size * t.n - 1 do snap[a] = sim.mem[a] end
    end
    return snap
end
local function changed_since(snap)
    local n = 0
    for a, v in pairs(snap) do if sim.mem[a] ~= v then n = n + 1 end end
    return n
end
local function touch(name)
    local f = assert(io.open(H.out(name), "wb"))
    f:write("on")
    f:close()
end
local function log_has(sub)
    for _, l in ipairs(require('imports/turbo/core/log').lines) do
        if l:find(sub, 1, true) then return true end
    end
    return false
end
local function moves_run(actions) return H.turbo().run("player_moves", { actions = actions }) end
local function run(name, cfg, opts) return H.turbo().run(name, cfg, opts) end

-- the first player of an AI club (4 per club in the world: team t has 2001 + (t - 2) * 4 ..)
local function ai_player(team)
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == team then
            local pid = sim:value("teamplayerlinks", rec, "playerid")
            if pid < 460000 and not sim:find_row("playerloans", "playerid", pid) then return pid end
        end
    end
    error("no player at team " .. team)
end

-- your players leaving your club come off your lists first (Turbo.dll's game call): a fake that says "not listed"
local function lists_not_listed()
    _G.TurboTransferList = function(code, pid, club)
        if code == 6 then return true, "status 0", "ok", 0, 0 end
        return false, "unexpected action " .. code, "failed", 0, 0
    end
end
lists_not_listed()

-- ---------------------------------------------------------------- a) moves.lua: no database arrival at your club without the opt-in
local db_moved   -- the player brought in by a database move under the opt-in (no development plan: used by b and c below)

H.case("transfer into your club without TurboPlayerMove: refused (also as a dry run), nothing written, op 11 and the opt-in file named, logged", function()
    _G.TurboPlayerMove = nil
    local pid = ai_player(3)
    local snap = db_bytes()
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = USER, months = 36, wage = 5000 } })
    H.eq(ok, false, msg)
    H.has(msg, string.format("player %d not moved: the transfer to your club (Arsenal, 1) would be a raw database move", pid))
    H.has(msg, "TurboPlayerMove is not available")
    H.has(msg, "The game's own transfer routine (Turbo.dll op 11) is needed for arrivals at your club")
    H.has(msg, "Nothing was changed"); H.has(msg, "create turbo_output\\" .. OPT_IN)
    H.eq(changed_since(snap), 0, "nothing written")
    H.eq(link_team(pid), 3, "still at his club")
    H.ok(log_has("arrival guard: player " .. pid .. " not moved"), "the refusal is in the log")
    local moves = require 'imports/turbo/core/moves'
    local okd, dmsg = moves.transfer(pid, USER, {}, true)
    H.eq(okd, false, dmsg); H.has(dmsg, "Turbo.dll op 11")
    H.eq(changed_since(snap), 0, "nothing written by the dry run")
end)

H.case("moves between AI clubs and out of your club: as before (no opt-in, no warning)", function()
    local pid = ai_player(4)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 5 } })
    H.eq(ok, true, msg); H.eq(link_team(pid), 5)
    H.ok(not msg:find("WARNING", 1, true), "no warning between AI clubs")
    local lp = ai_player(6)
    ok, msg = moves_run({ { action = "loan", playerid = lp, to_teamid = 8, months = 6 } })
    H.eq(ok, true, msg); H.eq(link_team(lp), 8)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = lp } })
    H.eq(ok, true, msg); H.eq(link_team(lp), 6, "back to his AI parent club")
    -- out of your club: a transfer, and the end of a loan TO your club (he goes back to his parent club, 7)
    ok, msg = moves_run({ { action = "transfer", playerid = W.USER_PLAYERS[4], to_teamid = 2 } })
    H.eq(ok, true, msg); H.eq(link_team(W.USER_PLAYERS[4]), 2)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = W.LOANED_IN } })
    H.eq(ok, true, msg); H.eq(link_team(W.LOANED_IN), 7)
    H.ok(not msg:find("WARNING", 1, true), "leaving your club: no warning")
end)

H.case("a loan to your club, the early return of your loaned-out player and his transfer back: refused without the opt-in", function()
    local pid = ai_player(8)
    local snap = db_bytes()
    local ok, msg = moves_run({ { action = "loan", playerid = pid, to_teamid = USER, months = 6 } })
    H.eq(ok, false, msg)
    H.has(msg, "the loan to your club (Arsenal, 1) would be a raw database move (a loan is always a database move")
    H.eq(sim:find_row("playerloans", "playerid", pid), nil, "no loan row")
    H.eq(changed_since(snap), 0, "nothing written")
    -- one of your players goes out on loan (allowed: he leaves your club) ...
    local mine = W.USER_PLAYERS[9]
    ok, msg = moves_run({ { action = "loan", playerid = mine, to_teamid = 9, months = 6 } })
    H.eq(ok, true, msg); H.eq(link_team(mine), 9)
    -- ... his early return is a database move into your club
    snap = db_bytes()
    ok, msg = moves_run({ { action = "terminate_loan", playerid = mine } })
    H.eq(ok, false, msg)
    H.has(msg, "the return from loan to your club"); H.has(msg, "the game ends the loan itself at its end date")
    H.eq(link_team(mine), 9, "still at the loan club"); H.ok(sim:find_row("playerloans", "playerid", mine), "loan row kept")
    -- ... and so is a transfer of him back (it would end his loan through the database)
    ok, msg = moves_run({ { action = "transfer", playerid = mine, to_teamid = USER } })
    H.eq(ok, false, msg); H.has(msg, "he is your club's player on loan at team 9")
    H.eq(changed_since(snap), 0, "nothing written")
end)

H.case("a loaned player of two AI clubs into your club: refused, the text says to end the loan first", function()
    local pid = ai_player(10)
    local ok, msg = moves_run({ { action = "loan", playerid = pid, to_teamid = 11, months = 6 } })
    H.eq(ok, true, msg)
    ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = USER } })
    H.eq(ok, false, msg)
    H.has(msg, "he is on loan at team 11 from team 10"); H.has(msg, "end the loan first (Terminate loan")
    H.eq(link_team(pid), 11)
end)

H.case("opt-in file present: the database move into your club runs, the summary and the log carry the warning; gone: refused again", function()
    touch(OPT_IN)
    local moves = require 'imports/turbo/core/moves'
    H.eq(moves.db_arrival_opted_in(), true)
    local pid = ai_player(12)
    local okd, dmsg = moves.transfer(pid, USER, {}, true)
    H.eq(okd, true, dmsg); H.has(dmsg, "WARNING: database move into your club")
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = USER, months = 36 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), USER)
    H.has(msg, "WARNING: database move into your club (opt-in turbo_output\\" .. OPT_IN .. ")")
    H.has(msg, "no development plan, growth record, morale record or squad status entry")
    H.ok(log_has("allowed as a database move"), "logged")
    db_moved = pid
    -- the early return of your loaned-out player too (W.USER_PLAYERS[9], out at 9)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = W.USER_PLAYERS[9] } })
    H.eq(ok, true, msg); H.has(msg, "WARNING: database move into your club"); H.eq(link_team(W.USER_PLAYERS[9]), USER)
    os.remove(H.out(OPT_IN))
    H.eq(moves.db_arrival_opted_in(), false)
    local other = ai_player(13)
    ok, msg = moves_run({ { action = "transfer", playerid = other, to_teamid = USER } })
    H.eq(ok, false, msg); H.eq(link_team(other), 13)
end)

H.case("with TurboPlayerMove the game's routine brings him in (no opt-in, no warning); a refusing game call never falls back", function()
    local calls = {}
    _G.TurboPlayerMove = function(code, pid, from, to, months, wage)
        calls[#calls + 1] = code
        if code == 9 then return true, "checked", "ok", true, false end
        local rec = sim:find_row("teamplayerlinks", "playerid", pid)
        sim:set_field(rec, sim.tables.teamplayerlinks.teamid, to)
        sim.player_team[pid] = to   -- the game sees him at your club: he has a development plan (the simulated game's rule)
        return true, "moved", "ok", true, true
    end
    local pid = ai_player(14)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = USER } })
    H.eq(ok, true, msg); H.eq(link_team(pid), USER)
    H.eq(table.concat(calls, ","), "9,1", "the game's check, then the game's move")
    H.has(msg, "done by the game"); H.ok(not msg:find("WARNING", 1, true), "the game's move: no warning")
    -- the call switched off (Turbo.dll's kill switch call_player_move_off.txt): refused, no database move instead
    _G.TurboPlayerMove = function() return false, "player_move: kill switch turbo_output\\call_player_move_off.txt is present", "failed" end
    local other = ai_player(241)
    local snap = db_bytes()
    ok, msg = moves_run({ { action = "transfer", playerid = other, to_teamid = USER } })
    H.eq(ok, false, msg); H.has(msg, "call_player_move_off.txt")
    H.eq(link_team(other), 241); H.eq(changed_since(snap), 0, "no database fallback")
    _G.TurboPlayerMove = nil
end)

-- ---------------------------------------------------------------- b) preset import onto your player without a development plan
H.case("preset import: your player with a plan -> 'development plan updated'; without one (database arrival) -> the warning; AI player -> neither", function()
    H.ok(db_moved, "the database arrival of the opt-in case")
    local ok, msg = run("player_presets", { mode = "export", playerid = 2001, miniface = false })
    H.eq(ok, true, msg)
    local csv = H.LE .. "/extensions/player_presets/Player_2001_2001.csv"
    -- your player with a plan (the simulated game has one for every player it sees at your club)
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = W.USER_PLAYERS[3], groups = { "attributes" } })
    H.eq(ok, true, msg); H.has(msg, "development plan updated")
    H.ok(not msg:find("no development plan", 1, true), "no warning with a plan")
    -- your player WITHOUT a plan: the database arrival (the game never made one)
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = db_moved, groups = { "attributes" } })
    H.eq(ok, true, msg)
    H.has(msg, "; no development plan: the game may undo this at its next growth step")
    H.ok(not msg:find("development plan updated", 1, true), "not claimed")
    -- the dry run says it too (a read of the plan flag; nothing written)
    local snap = db_bytes()
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = db_moved, groups = { "attributes" } }, { dry = true })
    H.eq(ok, true, msg); H.has(msg, "no development plan: the game may undo this at its next growth step")
    H.eq(changed_since(snap), 0, "dry run")
    -- another club's player has no plan: nothing said (the save keeps the table)
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = 2002, groups = { "attributes" } })
    H.eq(ok, true, msg)
    H.ok(not msg:find("development plan", 1, true), "AI player: no plan words")
    -- a profile-only import writes no plan field: no plan words either
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = db_moved, groups = { "profile" } })
    H.eq(ok, true, msg); H.ok(not msg:find("development plan", 1, true), "no attribute written: no plan words")
    -- a Live Editor build without the plan natives: your player's import says it was not checked
    local set = _G.PlayerSetValueInDevelopementPlan
    _G.PlayerSetValueInDevelopementPlan = nil
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = W.USER_PLAYERS[3], groups = { "attributes" } })
    _G.PlayerSetValueInDevelopementPlan = set
    H.eq(ok, true, msg); H.has(msg, "development plan not checked (this Live Editor build has no development-plan natives)")
end)

-- ---------------------------------------------------------------- c) team mass dev bonus counts the plans really written
local function team_pids(teamid)
    local out = {}
    for _, rec in ipairs(sim:rows("teamplayerlinks")) do
        if sim:value("teamplayerlinks", rec, "teamid") == teamid then out[#out + 1] = sim:value("teamplayerlinks", rec, "playerid") end
    end
    table.sort(out)
    return out
end

H.case("dev bonus (no LE development manager): 'development plan updated for N of M', your plan-less players named with the warning", function()
    for _, pid in ipairs(team_pids(USER)) do pset(pid, "potential", 95); pset(pid, "overallrating", 60) end
    local writes = sim:count_calls("PlayerSetValueInDevelopementPlan")
    local ok, msg = run("team_mass", { teamid = USER, actions = { "dev_bonus" }, bonus_xp = 1 })
    H.eq(ok, true, msg)
    local grown = tonumber(msg:match("dev bonus: %+1 on (%d+) players"))
    local synced, of = msg:match("development plan updated for (%d+) of (%d+)")
    H.ok(grown and synced, msg)
    H.eq(tonumber(of), grown, "of the players grown")
    -- every player of your club except the database arrival has a plan in the simulated game
    H.eq(tonumber(synced), grown - 1, "the plans really written")
    H.has(msg, string.format("; no development plan for 1 of your club's players (%d): the game may undo this at its next growth step", db_moved))
    H.ok(not msg:find("plans updated)", 1, true), "the old constant text is gone")
    H.ok(sim:count_calls("PlayerSetValueInDevelopementPlan") > writes, "plans written")
    -- an AI club: its players have no plan, said plainly
    ok, msg = run("team_mass", { teamid = 3, actions = { "dev_bonus" }, bonus_xp = 1 })
    H.eq(ok, true, msg); H.has(msg, "development plan updated for 0 of"); H.has(msg, "not your club's players: the save keeps the table")
    -- dry run: no plan claimed
    ok, msg = run("team_mass", { teamid = USER, actions = { "dev_bonus" }, bonus_xp = 1 }, { dry = true })
    H.eq(ok, true, msg); H.has(msg, "development plans not written (dry run)")
end)

H.case("no unmapped memory reads (moves, imports, dev bonus)", function() H.eq(sim.unmapped_reads, 0) end)

-- ---------------------------------------------------------------- d) create_player at your club: squad status entry at once
local function status_entries()
    local out, count = {}, sim:r32(W.psm + 0x14)
    for i = 0, count - 1 do out[sim:r32(W.status_begin + i * 8)] = sim:r8(W.status_begin + i * 8 + 4) end
    return out, count
end
local function fresh()
    sim = H.setup({ in_cm = true, le_27_1_2 = true })
    W.build(sim, { playerloans = true, fc27_status = true })
end

H.case("create at your club (memory path): the new player gets his squad status entry at once, summary and log say so", function()
    fresh()
    local _, before = status_entries()
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300001 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entry added at once (role 3, in memory)")
    local e, count = status_entries()
    H.eq(count, before + 1, "one entry more"); H.eq(e[300001], 3, "his entry, Rotation by default")
    H.ok(log_has("match crash guard after create_player (player 300001): squad status entry added at once"), "logged")
    -- an AI club: no squad status step
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = 3, playerid = 300002 })
    H.eq(ok, true, msg); H.ok(not msg:find("squad status", 1, true), "AI club: nothing about your squad status table")
    local _, after = status_entries()
    H.eq(after, count, "nothing added for an AI club's player")
end)

H.case("create at your club with the game's squad_role call: the game's AddPlayer and its role; a player the game does not see yet is reported", function()
    fresh()
    local f, calls = W.fake_squad_role(sim, { game_role = 4, club_extra = { 300003 } })
    _G.TurboSquadRole = f
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300003 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entry added at once (role 4, the game's AddPlayer)")
    H.eq(calls[#calls][1], 2, "code 2: the game picks his role"); H.eq(calls[#calls][2], 300003)
    -- the game does not see him at your club yet (a database-created row before a save and a load): the game's AddPlayer
    -- refuses, and since 2.0.4 the guard's memory path writes his entry in the same run (an entry is safer than none: the
    -- match load reads one for every team-sheet player)
    local _, before = status_entries()
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300004 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entry added at once (role 3, in memory)")
    local e, after = status_entries()
    H.eq(after, before + 1, "one entry added in memory"); H.eq(e[300004] ~= nil, true)
    _G.TurboSquadRole = nil
end)

H.case("create at your club: kill switches create_status_repair_off.txt and squad_status_repair_off.txt stop the step, the player is still created", function()
    fresh()
    touch("create_status_repair_off.txt")
    local _, before = status_entries()
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300005 })
    H.eq(ok, true, msg); H.eq(link_team(300005), USER, "created")
    H.has(msg, "squad status entry not checked now (kill switch turbo_output\\create_status_repair_off.txt)")
    local e, after = status_entries()
    H.eq(after, before, "nothing written"); H.eq(e[300005], nil)
    os.remove(H.out("create_status_repair_off.txt"))
    touch("squad_status_repair_off.txt")
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300006 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entry not checked: the match crash guard is off (kill switch turbo_output\\squad_status_repair_off.txt)")
    local _, after2 = status_entries()
    H.eq(after2, before, "nothing written")
    os.remove(H.out("squad_status_repair_off.txt"))
    -- both gone: the next create repairs every missing entry of your club (300005, 300006 and the new one)
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300007 })
    H.eq(ok, true, msg); H.has(msg, "squad status entry added at once")
    e = status_entries()
    H.ok(e[300005] and e[300006] and e[300007], "all three have an entry now")
end)

H.case("create at your club through the game's INSERT (TurboPlayerCreate): entry present after the game's move; queued: left to the guard", function()
    fresh()
    -- the fake game call adds the link row and, as the game's PlayerJoinedTeam handler does, the squad status entry
    _G.TurboPlayerCreate = function(code, payload)
        local row = { teamid = USER, playerid = 300008, jerseynumber = 60 }
        H.ok(_G.InsertDBTableRow("teamplayerlinks", row), "link row")
        local n = sim:r32(W.psm + 0x14)
        sim:w32(W.status_begin + n * 8, 300008); sim:w8(W.status_begin + n * 8 + 4, 3); sim:w32(W.psm + 0x14, n + 1)
        return true, "player created through the game's database", "ok", 127, true
    end
    local ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300008 })
    H.eq(ok, true, msg); H.has(msg, "created by the game"); H.has(msg, "squad status entry present")
    _G.TurboPlayerCreate = function() return true, "queued for the game thread", "queued", 0, false end
    ok, msg = run("create_player", { source = { playerid = 2005 }, teamid = USER, playerid = 300009 })
    H.eq(ok, true, msg)
    H.has(msg, "squad status entry: checked by the match crash guard at the next career event (the game creates him on its next tick)")
    H.ok(log_has("match crash guard after create_player (player 300009): squad status entry: checked by the match crash guard"), "logged")
    _G.TurboPlayerCreate = nil
end)

H.case("no unmapped memory reads (create)", function() H.eq(sim.unmapped_reads, 0) end)

_G.TurboTransferList = nil
H.finish()
