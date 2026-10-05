package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t18 team mass actions: long contract, morale, squad roles by age, block offers")

-- simulated game date 2027-01-15; the default new contract (60 months) ends in 2031
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true })

local util = require 'imports/turbo/core/util'

local function pval(pid, field)
    local rec = sim:find_row("players", "playerid", pid)
    return rec and sim:value("players", rec, field) or nil
end
local function pset(pid, field, v)
    local rec = sim:find_row("players", "playerid", pid)
    sim:set_field(rec, sim.tables.players[field], v)
end
local function team_pids(tid)
    local out = {}
    for pid in pairs(sim.player_team) do if sim.player_team[pid] == tid then out[#out + 1] = pid end end
    table.sort(out)
    return out
end
local function run(cfg) return H.turbo().run("team_mass", cfg) end

H.case("input is validated: team, actions", function()
    local ok, msg = run({ actions = { "morale" } })
    H.eq(ok, false); H.has(msg, "teamid is required")
    ok, msg = run({ teamid = 2, actions = { "teleport" } })
    H.eq(ok, false); H.has(msg, "unknown action")
    ok, msg = run({ teamid = 2, actions = {} })
    H.eq(ok, false); H.has(msg, "no action selected")
    ok, msg = run({ teamid = 99999, actions = { "morale" } })
    H.eq(ok, false); H.has(msg, "has no players")
end)

H.case("long contract: every player of the team, 60 months; other teams untouched", function()
    local mine = team_pids(2)
    H.eq(#mine, 4)
    for _, pid in ipairs(mine) do pset(pid, "contractvaliduntil", 2027) end
    local other = team_pids(3)[1]
    local other_before = pval(other, "contractvaliduntil")
    local ok, msg = run({ teamid = 2, actions = { "long_contract" } })
    H.eq(ok, true, msg); H.has(msg, "4 players to 60 months")
    for _, pid in ipairs(mine) do H.eq(pval(pid, "contractvaliduntil"), 2031, "contract of " .. pid) end
    H.eq(pval(other, "contractvaliduntil"), other_before, "another team keeps its contracts")
end)

H.case("long contract: a loaned-in player keeps the parent club's contract; a dead join date is repaired", function()
    -- the world's loaned-in player (W.LOANED_IN) plays for the user's club on loan from team 7
    local pid = team_pids(W.USER_TEAM)[2]
    pset(pid, "contractvaliduntil", 2027)
    pset(pid, "playerjointeamdate", 0)
    pset(W.LOANED_IN, "contractvaliduntil", 2027)
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "long_contract" } })
    H.eq(ok, true, msg); H.has(msg, "loaned-in players keep")
    H.eq(pval(pid, "contractvaliduntil"), 2031)
    H.eq(pval(pid, "playerjointeamdate"), util.gregorian_days_from_date(2027, 1, 15), "join date today")
    H.eq(pval(W.LOANED_IN, "contractvaliduntil"), 2027, "loan player untouched")
end)

H.case("morale: SetPlayerMorale 100 for each player of the team", function()
    local before = sim:count_calls("SetPlayerMorale")
    local ok, msg = run({ teamid = 5, actions = { "morale" } })
    H.eq(ok, true, msg)
    H.eq(sim:count_calls("SetPlayerMorale") - before, #team_pids(5), "one call per player")
    H.eq(sim.calls.SetPlayerMorale[#sim.calls.SetPlayerMorale][2], 100, "value")
end)

H.case("squad roles: your club only, Rotation from 19, Prospect below", function()
    local squad = team_pids(W.USER_TEAM)
    local young = squad[3]
    pset(young, "birthdate", util.gregorian_days_from_date(2008, 3, 10))   -- 18 on 2027-01-15
    local nineteen = squad[4]
    pset(nineteen, "birthdate", util.gregorian_days_from_date(2007, 1, 15)) -- 20 today
    local just_below = squad[5]
    pset(just_below, "birthdate", util.gregorian_days_from_date(2007, 1, 16)) -- 19 tomorrow -> still 19
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "squad_roles" } })
    H.eq(ok, true, msg); H.has(msg, "PlayerStatusManager")
    local roles = {}
    for pid, addr in pairs(W.role_entries) do roles[pid] = sim:r32(addr) end
    H.eq(roles[young], 5, "18 years: Prospect")
    H.eq(roles[nineteen], 3, "20 years: Rotation")
    H.eq(roles[just_below], 3, "19 years: Rotation")
    local rotation = 0
    for pid, r in pairs(roles) do if pid ~= young and r == 3 then rotation = rotation + 1 end end
    H.ok(rotation >= 20, "the rest of the squad is Rotation: " .. rotation)
    ok, msg = run({ teamid = 2, actions = { "squad_roles" } })
    H.eq(ok, false); H.has(msg, "your own club only")
end)

H.case("squad roles: a promised player (flag byte after the role) is found and his flags survive", function()
    local pid = team_pids(W.USER_TEAM)[6]
    local addr = W.role_entries[pid]   -- the address of the role byte itself (entry + 4)
    sim:wb(addr + 1, 1)   -- wasPromised
    sim:wb(addr + 2, 1)   -- renewalDismissed
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "squad_roles" } })
    H.eq(ok, true, msg)
    H.eq(sim:rb(addr), 3, "role byte written")
    H.eq(sim:rb(addr + 1), 1, "wasPromised kept"); H.eq(sim:rb(addr + 2), 1, "renewalDismissed kept")
end)

H.case("squad roles: a saved layout whose role byte is not inside the entry after the player id is not used", function()
    -- role_off 3 reads (and would write) the top byte of the player id: 0 for every id, so it "validates" as a role
    local role = require 'imports/turbo/features/squad_role'
    local game = require 'imports/turbo/core/game'
    local squad, count = game.user_squad()
    for _, bad in ipairs({ { offset = 0x18, size = 8, role_off = 3 }, { offset = 0x18, size = 8, role_off = 8 } }) do
        local layout, err = role.locate(squad, count, bad)
        H.ok(layout, tostring(err))
        H.eq(layout.offset, 0x18); H.eq(layout.size, 8)
        H.eq(layout.role_off, 4, "the role byte after the player id, not role_off " .. bad.role_off)
    end
    local good = role.locate(squad, count, { offset = 0x18, size = 8, role_off = 4 })
    H.eq(good.role_off, 4)
end)

H.case("Free Agents is not a club: no mass action on every free agent", function()
    local before = sim:count_calls("SetPlayerMorale")
    local ok, msg = run({ teamid = 111592, actions = { "long_contract", "morale" } })
    H.eq(ok, false); H.has(msg, "Free Agents is not a club")
    H.eq(sim:count_calls("SetPlayerMorale"), before, "no morale call")
end)

H.case("block offers: your own club only; without Turbo.dll's game call nothing is attempted", function()
    local ok, msg = run({ teamid = 2, actions = { "block_offers" } })
    H.eq(ok, false); H.has(msg, "your own club")
    ok, msg = run({ teamid = W.USER_TEAM, actions = { "block_offers" } })
    H.eq(ok, false); H.has(msg, "TurboTransferList")
end)

H.case("block offers: the game's toggle is called per player, an already blocked player is left alone", function()
    local blocked, calls = {}, {}
    local squad = team_pids(W.USER_TEAM)
    blocked[squad[2]] = true
    _G.TurboTransferList = function(code, pid, club)
        calls[#calls + 1] = { code, pid, club }
        if code == 7 then
            if blocked[pid] then return true, "player " .. pid .. " is blocked already", "ok", 1, 1 end
            blocked[pid] = true
            return true, "player " .. pid .. " blocked", "ok", 0, 1
        end
        return false, "unexpected code " .. tostring(code), "failed"
    end
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "block_offers" } })
    _G.TurboTransferList = nil
    H.eq(ok, true, msg)
    H.has(msg, "1 were already blocked"); H.has(msg, "1 players on loan skipped")
    local n = 0
    for _ in pairs(blocked) do n = n + 1 end
    H.eq(n, #squad - 1, "every player of the club except the loaned-in one is blocked")
    for _, c in ipairs(calls) do H.eq(c[1], 7, "code 7 = block"); H.eq(c[3], W.USER_TEAM, "his club") end
    H.eq(blocked[W.LOANED_IN], nil, "the loaned-in player is not touched")
end)

H.case("block offers: a player of your squad with a loan record tied to your club is skipped, not a failure (seen in game)", function()
    -- 2026-10-05, turbo04: 5 of Napoli's 37 had playerloans rows; moves.list refuses any player with one ("is on loan")
    local squad = team_pids(W.USER_TEAM)
    local own = squad[4]
    H.ok(_G.InsertDBTableRow("playerloans", { playerid = own, teamidloanedfrom = W.USER_TEAM, loandateend = 0 }), "loan row added")
    local calls = {}
    _G.TurboTransferList = function(code, pid, club)
        calls[#calls + 1] = pid
        return true, "blocked", "ok", 0, 1
    end
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "block_offers" } })
    _G.TurboTransferList = nil
    H.eq(ok, true, msg)
    H.has(msg, "2 players on loan skipped")
    for _, pid in ipairs(calls) do H.ok(pid ~= own and pid ~= W.LOANED_IN, "no call for a player on loan") end
    H.eq(#calls, #squad - 2)
    local rec = sim:find_row("playerloans", "playerid", own)
    H.ok(_G.DeleteDBTableRowByAddr("playerloans", string.format("%d", rec)) ~= false, "loan row removed again")
end)

H.case("block offers: a refusal of the game is reported, the other players are still done", function()
    local squad = team_pids(W.USER_TEAM)
    local bad = squad[3]
    _G.TurboTransferList = function(code, pid, club)
        if pid == bad then return false, "the game refused", "failed" end
        return true, "ok", "ok", 0, 1
    end
    local ok, msg = run({ teamid = W.USER_TEAM, actions = { "block_offers" } })
    _G.TurboTransferList = nil
    H.eq(ok, false); H.has(msg, "1 failed (the game refused)")
end)

H.case("all actions: each one runs, one failing action does not stop the others", function()
    local before = sim:count_calls("SetPlayerMorale")
    local ok, msg = run({ teamid = W.USER_TEAM, actions = "all" })
    H.eq(ok, true, msg)
    H.has(msg, "long contract:"); H.has(msg, "squad roles:"); H.has(msg, "morale and happiness:")
    H.has(msg, "block offers: not done")
    H.ok(sim:count_calls("SetPlayerMorale") > before, "morale ran")
    for _, pid in ipairs(team_pids(W.USER_TEAM)) do
        if pid ~= W.LOANED_IN then H.eq(pval(pid, "contractvaliduntil"), 2031, "contract of " .. pid) end
    end
end)

H.case("a block offers hook (tests / another host), when set, is used", function()
    local tm = require 'imports/turbo/features/team_mass'
    local seen = {}
    tm.hooks.block_offers = function(pids) local n = 0; for _ in pairs(pids) do n = n + 1 end; seen.block = n; return true, n .. " players blocked" end
    local ok, msg = run({ teamid = 2, actions = { "block_offers" } })
    tm.hooks.block_offers = nil
    H.eq(ok, true, msg)
    H.eq(seen.block, 4)
    H.has(msg, "4 players blocked")
end)

H.case("dry run writes nothing", function()
    H.write_config({ turbo = { dry_run = true } })
    local pid = team_pids(6)[1]
    pset(pid, "contractvaliduntil", 2027)
    local before = sim:count_calls("SetPlayerMorale")
    local ok, msg = run({ teamid = 6, actions = { "long_contract", "morale" } })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]")
    H.eq(pval(pid, "contractvaliduntil"), 2027, "unchanged")
    H.eq(sim:count_calls("SetPlayerMorale"), before, "no native call")
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
