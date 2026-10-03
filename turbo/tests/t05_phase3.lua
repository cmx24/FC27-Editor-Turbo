package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t05 bulk edit, player moves, db edit")

local sim = H.setup({ in_cm = true })
W.build(sim, {})

H.case("bulk_edit: user squad, age filter, field + actions + development", function()
    H.write_config({ modules = { bulk_edit = {
        scope = { user_team = true },
        filters = { max_age = 25 },
        set = { potential = 95, isretiring = 0 },
        actions = { fitness = 95, development = { xp_multiplier = 2, bonus_xp = 100, no_decline = true } },
    } } })
    local ok, msg = H.turbo().run("bulk_edit")
    H.eq(ok, true, msg)
    -- birth years 1995 + (i % 12); age on 2027-01-15 with birthday 10 Mar: 2027 - year - 1
    local expected = 0
    for i = 1, 26 do
        local age = 2027 - (1995 + (i % 12)) - 1
        if age <= 25 then expected = expected + 1 end
    end
    H.has(msg, string.format("%d players (your squad)", expected))
    H.eq(sim:count_calls("SetPlayerFitness"), expected, "fitness calls")
    H.eq(sim:count_calls("PlayerDevelopmentManagerAddPlayer"), expected, "dev plans")
    H.eq(sim:count_calls("PlayerDevelopmentManagerSave"), 1, "saved once")
    local a = sim.calls.PlayerDevelopmentManagerAddPlayer[1]
    H.eq(a[2], 2.0, "multiplier"); H.eq(a[3], 100, "bonus"); H.eq(a[4], true, "no decline")
    local young = sim:find_row("players", "playerid", 1011)   -- born 2006 -> 20
    H.eq(sim:value("players", young, "potential"), 95, "young edited")
    local old = sim:find_row("players", "playerid", 1012)     -- born 1995 -> 31
    H.eq(sim:value("players", old, "potential"), 82, "old untouched")
end)

H.case("bulk_edit refuses bad values before any write", function()
    H.write_config({ modules = { bulk_edit = { scope = { user_team = true }, filters = {}, set = { potential = 200 }, actions = {} } } })
    local ok, msg = H.turbo().run("bulk_edit")
    H.eq(ok, false); H.has(msg, "potential=200 is outside the field range 0..127")
    ok, msg = H.turbo().run("bulk_edit", { set = { playerid = 5 } })
    H.eq(ok, false); H.has(msg, "playerid cannot be bulk-edited")
    ok, msg = H.turbo().run("bulk_edit", { set = {}, actions = { fitness = 99 } })
    H.eq(ok, false); H.has(msg, "actions.fitness must be 5..95")
    ok, msg = H.turbo().run("bulk_edit", { set = {}, actions = { sharpness = 99 } })
    H.eq(ok, false); H.has(msg, "unknown action: sharpness")
end)

H.case("bulk_edit scope all needs confirm_all", function()
    local base = { scope = { all = true }, filters = {}, set = { isretiring = 0 }, actions = {} }
    local ok, msg = H.turbo().run("bulk_edit", base)
    H.eq(ok, false); H.has(msg, "confirm_all")
    base.confirm_all = true
    ok, msg = H.turbo().run("bulk_edit", base)
    H.eq(ok, true, msg); H.has(msg, "(all players)")
end)

H.case("bulk_edit teamids + overall + positions filters", function()
    local ok, msg = H.turbo().run("bulk_edit", { scope = { teamids = { 2, 3 } }, filters = { min_overall = 57, positions = { 2, 3, 4 } },
        set = { socklengthcode = 2 }, actions = {}, confirm_all = false })
    H.eq(ok, true, msg)
    -- teams 2,3: players j=1..4 ovr 55+j pos j -> j in {2,3,4} and ovr>=57 -> j=2,3,4 -> 3 per team
    H.has(msg, "6 players (teams 2,3)")
end)

H.case("bulk_edit rejects unknown filters", function()
    local ok, msg = H.turbo().run("bulk_edit", { scope = { user_team = true }, filters = { min_height = 180 }, set = { isretiring = 0 }, actions = {} })
    H.eq(ok, false); H.has(msg, "unknown filter: min_height")
end)

H.case("player_moves validates every action before running any", function()
    H.write_config({ modules = { player_moves = { actions = {
        { action = "transfer", playerid = 2001, to_teamid = 7, fee = 1000000, wage = 20000, months = 36 },
        { action = "loan", playerid = 2002, to_teamid = 99999, months = 12 },
    } } } })
    local ok, msg = H.turbo().run("player_moves")
    H.eq(ok, false); H.has(msg, "action 2: team 99999 not found")
    H.eq(sim:count_calls("cTransferPlayer"), 0, "nothing ran")
end)

H.case("player_moves runs transfer, loan, release, terminate, list by scope", function()
    H.write_config({ modules = { player_moves = { actions = {
        { action = "transfer", playerid = 2001, to_teamid = 7, fee = 1000000, wage = 20000, months = 36 },
        { action = "loan", playerid = 2002, to_teamid = 241, months = 6 },
        { action = "release", playerid = 2003 },
        { action = "terminate_loan", playerid = 2004 },
        { action = "transfer_list", scope = { user_team = true }, filters = { max_overall = 65 } },
        { action = "unlist", playerid = 1001 },
    } } } })
    local ok, msg = H.turbo().run("player_moves")
    H.eq(ok, true, msg)
    local t = sim.calls.cTransferPlayer[1]
    -- cTransferPlayer(playerid, from_teamid, to_teamid, transfersum, release_clause, wage, contract_length)
    H.eq(t[1], 2001); H.eq(t[2], 0); H.eq(t[3], 7); H.eq(t[4], 1000000); H.eq(t[5], -1); H.eq(t[6], 20000); H.eq(t[7], 36)
    local l = sim.calls.cLoanPlayer[1]
    -- cLoanPlayer(playerid, from_teamid, to_teamid, loan_length, loantobuy)
    H.eq(l[1], 2002); H.eq(l[3], 241); H.eq(l[4], 6); H.eq(l[5], -1)
    H.eq(sim:count_calls("cReleasePlayer"), 1, "release")
    H.eq(sim:count_calls("TerminateLoan"), 1, "terminate")
    H.eq(sim:count_calls("cAddPlayerToTransferList"), 5, "ovr 61..65 listed")
    H.eq(sim:count_calls("cRemovePlayerFromLists"), 1, "unlist")
    H.has(msg, "transfer_list x5")
end)

H.case("player_moves delete: refused without confirm, then deletes exactly one player", function()
    local before = sim:count_calls("DeletePlayer")
    H.write_config({ modules = { player_moves = { actions = { { action = "delete", playerid = 2005 } } } } })
    local ok, msg = H.turbo().run("player_moves")
    H.eq(ok, false); H.has(msg, "needs \"confirm\": true")
    H.eq(sim:count_calls("DeletePlayer"), before, "nothing deleted without confirm")
    H.write_config({ modules = { player_moves = { actions = { { action = "delete", playerid = 2005, confirm = true } } } } })
    ok, msg = H.turbo().run("player_moves")
    H.eq(ok, true, msg)
    H.has(msg, "delete x1")
    H.eq(sim:count_calls("DeletePlayer"), before + 1, "one call")
    local call = sim.calls.DeletePlayer[#sim.calls.DeletePlayer]
    H.eq(call[1], 2005, "that player"); H.eq(call[2], 0, "team looked up by Live Editor")
    H.eq(sim:find_row("players", "playerid", 2005), nil, "record marked deleted")
    H.write_config({ modules = { player_moves = { actions = { { action = "delete", playerid = 99999999, confirm = true } } } } })
    ok, msg = H.turbo().run("player_moves")
    H.eq(ok, false); H.has(msg, "not found")
end)

H.case("db_edit: edit teams by condition, float match, all-rows guard", function()
    H.write_config({ modules = { db_edit = { edits = {
        { table = "teams", where = { teamid = 7 }, set = { teamname = "Everton FC", transferbudget = 50000000 } },
        { table = "formations", where = { offset1x = 0.25 }, set = { formationid = 9 } },
    } } } })
    local ok, msg = H.turbo().run("db_edit")
    H.eq(ok, true, msg); H.has(msg, "teams: 1 rows; formations: 1 rows")
    local r = sim:find_row("teams", "teamid", 7)
    H.eq(sim:value("teams", r, "teamname"), "Everton FC", "name")
    H.eq(sim:value("teams", r, "transferbudget"), 50000000, "budget")
    H.eq(sim:value("formations", sim:find_row("formations", "teamid", 1), "formationid"), 9, "float where")
    H.write_config({ modules = { db_edit = { edits = { { table = "players", where = {}, set = { isretiring = 1 } } } } } })
    ok, msg = H.turbo().run("db_edit")
    H.eq(ok, false); H.has(msg, "allow_all_rows")
    H.write_config({ modules = { db_edit = { edits = { { table = "teams", where = { teamid = 7 }, set = { teamname = string.rep("y", 40) } } } } } })
    ok, msg = H.turbo().run("db_edit")
    H.eq(ok, false); H.has(msg, "at most 30 bytes")
    H.eq(sim:value("teams", r, "teamname"), "Everton FC", "unchanged after refusal")
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
