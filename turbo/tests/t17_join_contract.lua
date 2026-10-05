package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t17 join date + contract: players that come to a club (transfer, loan, create, import) are valid for the squad hub / team management")

-- The simulated game date is 2027-01-15: the season runs 2026/27, a contract ends in June of its year, so the
-- earliest contract still running ends in 2027; the default new contract (60 months) ends in 2031.
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, development = true, player_fields = { { name = "wage", depth = 20 } } })

local util = require 'imports/turbo/core/util'
local moves = require 'imports/turbo/core/moves'

local TODAY = util.gregorian_days_from_date(2027, 1, 15)
local OLD_JOIN = util.gregorian_days_from_date(2019, 8, 1)

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
local function run(mod, cfg) return H.turbo().run(mod, cfg) end
local function moves_run(actions) return run("player_moves", { actions = actions }) end

H.case("contract_values: kept when it still runs, replaced when 0 / over, join date 0 or future replaced", function()
    local j, c = moves.contract_values(OLD_JOIN, 2028, {})
    H.eq(j, OLD_JOIN, "valid join date kept"); H.eq(c, 2028, "valid contract kept")
    j, c = moves.contract_values(OLD_JOIN, 2027, {})
    H.eq(c, 2027, "a contract ending June 2027 still runs in January 2027")
    j, c = moves.contract_values(0, 0, {})
    H.eq(j, TODAY, "no join date: today"); H.eq(c, 2031, "no contract: 60 months")
    j, c = moves.contract_values(OLD_JOIN, 2026, {})
    H.eq(c, 2031, "expired contract replaced")
    j, c = moves.contract_values(TODAY + 400, 2030, {})
    H.eq(j, TODAY, "a join date in the future is replaced")
    j, c = moves.contract_values(OLD_JOIN, 2030, { new_join = true })
    H.eq(j, TODAY, "new_join"); H.eq(c, 2030, "valid contract still kept")
    j, c = moves.contract_values(OLD_JOIN, 2030, { new_join = true, months = 12 })
    H.eq(c, 2027, "a new contract of 12 months")
end)

H.case("transfer from another club: fresh join date and a valid contract even from a stale one", function()
    local pid = 2002
    H.eq(link_team(pid), 2)
    pset(pid, "playerjointeamdate", OLD_JOIN)
    pset(pid, "contractvaliduntil", 2026)
    local snap = db_bytes()
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 3 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 3)
    H.eq(pval(pid, "playerjointeamdate"), TODAY, "joined today")
    H.eq(pval(pid, "contractvaliduntil"), 2031, "60 months by default")
    H.ok(changed_since(snap) > 0, "something written")
    -- explicit length
    pid = 2003
    ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 4, months = 12 } })
    H.eq(ok, true, msg)
    H.eq(pval(pid, "contractvaliduntil"), 2027)
    H.eq(pval(pid, "playerjointeamdate"), TODAY)
end)

H.case("transfer dry run writes nothing", function()
    local pid = 2005
    pset(pid, "playerjointeamdate", 0)
    pset(pid, "contractvaliduntil", 0)
    local snap = db_bytes()
    local ok, msg = moves.transfer(pid, 4, {}, true)
    H.eq(ok, true, msg)
    H.eq(changed_since(snap), 0, "dry run wrote nothing")
    H.eq(pval(pid, "playerjointeamdate"), 0)
end)

H.case("loan: joins the loan club today, keeps a valid contract; ending the loan keeps the date, repairs a dead contract", function()
    local pid = 2006
    pset(pid, "playerjointeamdate", OLD_JOIN)
    pset(pid, "contractvaliduntil", 2031)
    local ok, msg = moves_run({ { action = "loan", playerid = pid, to_teamid = 5, months = 6 } })
    H.eq(ok, true, msg)
    H.eq(pval(pid, "playerjointeamdate"), TODAY, "joined the loan club today")
    H.eq(pval(pid, "contractvaliduntil"), 2031, "contract kept")
    pset(pid, "contractvaliduntil", 0)
    ok, msg = moves_run({ { action = "terminate_loan", playerid = pid } })
    H.eq(ok, true, msg)
    H.eq(pval(pid, "playerjointeamdate"), TODAY, "join date not touched on the way back")
    H.eq(pval(pid, "contractvaliduntil"), 2031, "a dead contract is repaired")
end)

H.case("create (blank, copy of a player with a stale contract, copy with a valid one): join date today, contract valid", function()
    local ok, msg = run("create_player", { source = { blank = true }, teamid = 4, set = { overallrating = 50, potential = 60 } })
    H.eq(ok, true, msg)
    local pid = tonumber(msg:match("new player (%d+)"))
    H.ok(pid, msg)
    H.has(msg, "joined today")
    H.eq(pval(pid, "playerjointeamdate"), TODAY)
    H.eq(pval(pid, "contractvaliduntil"), 2031)

    pset(2007, "playerjointeamdate", OLD_JOIN)
    pset(2007, "contractvaliduntil", 2020)
    ok, msg = run("create_player", { source = { playerid = 2007 }, teamid = 4 })
    H.eq(ok, true, msg)
    pid = tonumber(msg:match("new player (%d+)"))
    H.eq(pval(pid, "playerjointeamdate"), TODAY, "joins today, not on the source's date")
    H.eq(pval(pid, "contractvaliduntil"), 2031, "expired source contract replaced")

    pset(2008, "contractvaliduntil", 2031)
    ok, msg = run("create_player", { source = { playerid = 2008 }, teamid = 111592 })
    H.eq(ok, true, msg)
    pid = tonumber(msg:match("new player (%d+)"))
    H.eq(pval(pid, "contractvaliduntil"), 2031, "valid source contract kept")
    H.eq(pval(pid, "playerjointeamdate"), TODAY)
end)

H.case("create dry run reports the plan and writes nothing", function()
    local snap = db_bytes()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = run("create_player", { source = { blank = true }, teamid = 4 })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg)
    H.has(msg, "joined today")
    H.eq(changed_since(snap), 0, "dry run wrote nothing")
end)

H.case("import as a new player from a preset file whose contract is over: valid contract, join date today", function()
    pset(2009, "playerjointeamdate", 0)
    pset(2009, "contractvaliduntil", 2020)
    local ok, msg = run("player_presets", { mode = "export", playerid = 2009, miniface = false })
    H.eq(ok, true, msg)
    local csv = H.LE .. "/extensions/player_presets/Player_2009_2009.csv"
    ok, msg = run("create_player", { source = { file = csv }, teamid = 6 })
    H.eq(ok, true, msg)
    local pid = tonumber(msg:match("new player (%d+)"))
    H.ok(pid, msg)
    H.eq(pval(pid, "playerjointeamdate"), TODAY)
    H.eq(pval(pid, "contractvaliduntil"), 2031)

    -- onto an existing player with the contract group: the file's dead values are repaired, valid ones kept
    pset(2010, "playerjointeamdate", OLD_JOIN)
    pset(2010, "contractvaliduntil", 2030)
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = 2010, groups = { "contract" } })
    H.eq(ok, true, msg)
    H.has(msg, "contract made valid")
    H.eq(pval(2010, "playerjointeamdate"), TODAY, "the file's join date 0 became today")
    H.eq(pval(2010, "contractvaliduntil"), 2031, "the file's expired contract replaced")
    -- dry run writes nothing
    pset(2010, "playerjointeamdate", OLD_JOIN)
    local snap = db_bytes()
    H.write_config({ turbo = { dry_run = true } })
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = 2010, groups = { "contract" } })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg)
    H.eq(changed_since(snap), 0, "dry run wrote nothing")
end)

H.case("development plan: import onto your player and a move keep the plan equal to the players table", function()
    local mine = W.USER_PLAYERS[3]
    local ok, msg = run("player_presets", { mode = "export", playerid = 2001, miniface = false })
    H.eq(ok, true, msg)
    local csv = H.LE .. "/extensions/player_presets/Player_2001_2001.csv"
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = mine, groups = { "attributes" } })
    H.eq(ok, true, msg)
    H.has(msg, "development plan updated")
    H.eq(sim.dev_plans[mine].finishing, pval(2001, "finishing"), "plan got the imported attribute")
    H.eq(sim.dev_plans[mine].finishing, pval(mine, "finishing"), "plan equals the table")
    -- a plan that drifted (the game's old values) is brought back to the table's values when the player moves
    local inc = 2012   -- joins your club: the game then holds a plan for him (simulated), with old values
    sim.player_team[inc] = W.USER_TEAM
    sim.dev_plans[inc] = { finishing = 1 }
    ok, msg = moves_run({ { action = "transfer", playerid = inc, to_teamid = W.USER_TEAM } })
    H.eq(ok, true, msg)
    H.has(msg, "development plan refreshed")
    H.eq(sim.dev_plans[inc].finishing, pval(inc, "finishing"), "plan equals the table after the move")
    -- another club's player has no plan: nothing is written into any
    local before = sim:count_calls("PlayerSetValueInDevelopementPlan")
    ok, msg = run("player_presets", { mode = "import", file = csv, playerid = 2010, groups = { "attributes" } })
    H.eq(ok, true, msg)
    H.eq(sim:count_calls("PlayerSetValueInDevelopementPlan"), before, "no plan: no plan write")
end)

H.case("transfer keeps his wage unless one is given (0 = keep)", function()
    local pid = 2004
    pset(pid, "wage", 345000)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 3, wage = 0 } })
    H.eq(ok, true, msg)
    H.eq(pval(pid, "wage"), 345000, "wage 0 keeps the wage he had")
    pid = 2005
    pset(pid, "wage", 345000)
    ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 4, wage = 80000 } })
    H.eq(ok, true, msg)
    H.eq(pval(pid, "wage"), 80000, "an explicit wage is written")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
