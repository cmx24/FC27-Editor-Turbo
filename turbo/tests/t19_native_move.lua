package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t19 moves through the game's own call (TurboPlayerMove): check first, contract fields, then the game moves him")

-- simulated game date 2027-01-15; the default new contract (60 months) ends in 2031
local sim = H.setup({ in_cm = true, le_27_1_2 = true })
W.build(sim, { playerloans = true, development = true,
    player_fields = { { name = "wage", depth = 20 }, { name = "releaseclause", depth = 24 } } })

local util = require 'imports/turbo/core/util'
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
local function moves_run(actions) return H.turbo().run("player_moves", { actions = actions }) end

-- the fake game call: records what Lua had written at the moment of each call, and does what the game does
local calls, refuse_check, refuse_move = {}, nil, nil
local function install()
    calls, refuse_check, refuse_move = {}, nil, nil
    _G.TurboPlayerMove = function(code, pid, from, to, months, wage)
        calls[#calls + 1] = { code = code, pid = pid, from = from, to = to, months = months, wage = wage,
            cvu = pval(pid, "contractvaliduntil"), join = pval(pid, "playerjointeamdate"), team = link_team(pid),
            pay = pval(pid, "wage"), clause = pval(pid, "releaseclause") }
        if code == 9 then
            if refuse_check then return false, refuse_check, "failed" end
            return true, "checked", "ok", true, false
        end
        if refuse_move then return false, refuse_move, "failed" end
        local rec = sim:find_row("teamplayerlinks", "playerid", pid)
        sim:set_field(rec, sim.tables.teamplayerlinks.teamid, to)
        pset(pid, "playerjointeamdate", TODAY)   -- the game writes the join date itself
        sim.player_team[pid] = to
        return true, "moved", "ok", true, true
    end
end
install()

local USER = W.USER_TEAM

H.case("transfer to another club: the game checks, then Lua writes the contract fields, then the game moves him", function()
    install()
    local pid = 2002
    H.eq(link_team(pid), 2)
    pset(pid, "playerjointeamdate", OLD_JOIN)
    pset(pid, "contractvaliduntil", 2027)
    pset(pid, "wage", 345000)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 3, months = 60 } })
    H.eq(ok, true, msg)
    H.eq(#calls, 2, "a check and the move")
    H.eq(calls[1].code, 9); H.eq(calls[1].team, 2, "the check ran before anything was written")
    H.eq(calls[1].cvu, 2027, "contract untouched at the check")
    H.eq(calls[2].code, 1); H.eq(calls[2].from, 2); H.eq(calls[2].to, 3); H.eq(calls[2].months, 60)
    H.eq(calls[2].cvu, 2031, "the contract end is written BEFORE the game's move (its join handlers read it)")
    H.eq(calls[2].team, 2, "Lua did not write the club link: the game does")
    H.eq(calls[2].join, OLD_JOIN, "Lua did not write the join date: the game does")
    H.eq(calls[2].wage, 345000, "his own wage is passed on when none is given")
    H.eq(link_team(pid), 3, "he is at the new club")
    H.eq(pval(pid, "wage"), 345000, "wage kept")
    H.has(msg, "done by the game")
end)

H.case("a wage and a release clause given are written before the call", function()
    install()
    local pid = 2003
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 4, months = 24, wage = 90000, release_clause = 5000000 } })
    H.eq(ok, true, msg)
    H.eq(calls[2].pay, 90000); H.eq(calls[2].clause, 5000000); H.eq(calls[2].wage, 90000); H.eq(calls[2].months, 24)
end)

H.case("the game refuses at the check: nothing is written at all", function()
    install()
    refuse_check = "the club would be over the squad limit"
    local pid = 2006
    local team = link_team(pid)
    pset(pid, "contractvaliduntil", 2027)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 7, months = 60 } })
    H.eq(ok, false); H.has(msg, "squad limit"); H.has(msg, "nothing was changed")
    H.eq(#calls, 1, "only the check was called")
    H.eq(pval(pid, "contractvaliduntil"), 2027, "contract not touched")
    H.eq(link_team(pid), team, "not moved")
end)

H.case("the game refuses the move itself: the report says the contract fields were written", function()
    install()
    refuse_move = "the game refused"
    local pid = 2010
    local team = link_team(pid)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 7, months = 60 } })
    H.eq(ok, false); H.has(msg, "contract fields were written but the game refused the move")
    H.eq(link_team(pid), team, "still at his club")
end)

H.case("release: the game releases him first, wage and release clause are reset afterwards", function()
    install()
    local pid = 2001
    pset(pid, "wage", 123000)
    pset(pid, "releaseclause", 7000000)
    local ok, msg = moves_run({ { action = "release", playerid = pid } })
    H.eq(ok, true, msg)
    H.eq(calls[#calls].code, 2, "native release")
    H.eq(calls[#calls].pay, 123000, "his wage was still there when the game worked out the release")
    H.eq(calls[#calls].clause, 7000000)
    H.eq(link_team(pid), W.FREE_AGENTS or 111592)
    H.eq(pval(pid, "wage"), 0, "wage reset after"); H.eq(pval(pid, "releaseclause"), 0, "clause reset after")
end)

H.case("a loaned player's move ends his loan: that stays a database move, the game call is not used", function()
    install()
    local before = #calls
    local ok, msg = moves_run({ { action = "transfer", playerid = W.LOANED_IN, to_teamid = 4, months = 60 } })
    H.eq(ok, true, msg)
    H.eq(#calls, before, "no TurboPlayerMove call")
    H.eq(link_team(W.LOANED_IN), 4)
end)

H.case("without Turbo.dll's call the moves are database moves as before (the club link is written by Lua)", function()
    _G.TurboPlayerMove = nil
    local pid = 2014
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 6, months = 60 } })
    H.eq(ok, true, msg)
    H.eq(link_team(pid), 6)
    H.eq(pval(pid, "playerjointeamdate"), TODAY, "join date written by Lua")
end)

H.case("dry run: no call at all", function()
    install()
    H.write_config({ turbo = { dry_run = true } })
    local ok, msg = moves_run({ { action = "transfer", playerid = 2005, to_teamid = 8, months = 60 } })
    H.write_config({ turbo = { dry_run = false } })
    H.eq(ok, true, msg); H.eq(#calls, 0, "no game call in a dry run")
end)

H.case("create player: the club link is a database row (the game's queries do not see InsertDBTableRow rows): no game call", function()
    -- checked in game 2026-10-05: a player created at Free Agents was "not in team 111592" for the game's IsPlayerInTeam
    install()
    local ok, msg = H.turbo().run("create_player", { source = { playerid = 2005 }, teamid = 3, jersey = 77 })
    H.eq(ok, true, msg)
    H.eq(#calls, 0, "no TurboPlayerMove call")
    local rec = sim:find_row("teamplayerlinks", "playerid", 2057)
    H.ok(rec, "the new player's link row"); H.eq(sim:value("teamplayerlinks", rec, "teamid"), 3)
    H.eq(sim:value("teamplayerlinks", rec, "form"), 3, "average form like the game's own links, not the field minimum")
end)

H.case("a check the DLL only queued has not run: nothing is written, the move is not called", function()
    install()
    local real = _G.TurboPlayerMove
    _G.TurboPlayerMove = function(code, ...)
        if code == 9 then calls[#calls + 1] = { code = 9 }; return true, "game call queued", "queued" end
        return real(code, ...)
    end
    local pid = 2007
    local team = link_team(pid)
    pset(pid, "contractvaliduntil", 2027)
    local ok, msg = moves_run({ { action = "transfer", playerid = pid, to_teamid = 8, months = 60 } })
    H.eq(ok, false); H.has(msg, "did not run right now"); H.has(msg, "nothing was changed")
    H.eq(#calls, 1, "only the check")
    H.eq(pval(pid, "contractvaliduntil"), 2027, "contract not touched")
    H.eq(link_team(pid), team, "not moved")
end)

-- The real native: bridge.install_natives defines TurboPlayerMove on top of Turbo.dll's turbo_game_call export; the words
-- are packed as turbogui/src/core/player_move.h says (op 11)
H.case("TurboPlayerMove from the bridge: op 11 words, outputs read back as booleans, bad values refused before the call", function()
    _G.TurboPlayerMove = nil
    local bridge = require 'imports/turbo/bridge'
    local OP, STATUS, SEQ, RSEQ, ARGS, OUT, TEXT = 0x2020, 0x2024, 0x2028, 0x202C, 0x2030, 0x2050, 0x2060
    local seen, answer = {}, { status = 1, text = "moved", out0 = 1, out1 = 1 }
    local function fake_turbo_game_call()
        local mb = TURBO_STATE.bridge.mailbox
        local seq = sim:r32(mb + SEQ)
        local w1, w2, w3 = sim:r64(mb + ARGS + 8), sim:r64(mb + ARGS + 16), sim:r64(mb + ARGS + 24)
        seen[#seen + 1] = { op = sim:r32(mb + OP), comm = sim:r64(mb + ARGS), code = w1 & 0xFF, months = (w1 >> 8) & 0xFF,
            high = w1 >> 16, pid = w2 & 0xFFFFFFFF, wage = w2 >> 32, from = w3 & 0xFFFFFFFF, to = w3 >> 32 }
        sim:wstr(mb + TEXT, answer.text)
        sim:w64(mb + OUT, answer.out0)
        sim:w64(mb + OUT + 8, answer.out1)
        sim:w32(mb + RSEQ, seq)
        sim:w32(mb + STATUS, answer.status)
    end
    TURBO_STATE.bridge.next_dll_check = 0
    H.ok(bridge.mailbox_address() ~= nil, "mailbox found through bridge_dll.json")
    local real_loadlib = package.loadlib
    package.loadlib = function(path, sym)
        if sym == "turbo_game_call" then return fake_turbo_game_call end
        if sym == "turbo_game_pump" then return function() end end
        return nil
    end
    bridge.install_natives()
    H.eq(type(_G.TurboPlayerMove), "function", "defined by install_natives")
    local ok, text, status, from_ok, to_ok = _G.TurboPlayerMove(1, 200123, 44, 111592, 60, 345000)
    H.eq(ok, true, text); H.eq(status, "ok"); H.eq(text, "moved"); H.eq(from_ok, true); H.eq(to_ok, true)
    local s = seen[#seen]
    H.eq(s.op, 11); H.eq(s.code, 1); H.eq(s.months, 60); H.eq(s.high, 0, "nothing above bit 15 of the mode word")
    H.eq(s.pid, 200123); H.eq(s.wage, 345000); H.eq(s.from, 44); H.eq(s.to, 111592)
    -- a refusal after the call ran still carries its read-backs; -1 = not read
    answer = { status = -1, text = "the game did not move him", out0 = 0, out1 = -1 }
    ok, text, status, from_ok, to_ok = _G.TurboPlayerMove(9, 7, 1, 2, 0, 0)
    H.eq(ok, false); H.eq(status, "failed"); H.has(text, "did not move"); H.eq(from_ok, false); H.eq(to_ok, nil)
    H.eq(seen[#seen].code, 9); H.eq(seen[#seen].months, 0)
    -- out of range: refused here, no call (the packing would truncate it)
    local n = #seen
    for _, a in ipairs({ { 3, 1, 1, 2, 0, 0 }, { 1, 0, 1, 2, 0, 0 }, { 1, 5, 0, 2, 0, 0 }, { 1, 5, 1, 2, 121, 0 },
                         { 1, 5, 1, 2, 12, -1 }, { 1, 5, 1, 2, 12, 10000001 }, { 1, 5, 1, -2, 12, 0 } }) do
        ok, text, status = _G.TurboPlayerMove(table.unpack(a))
        H.eq(ok, false, "refused: " .. table.concat(a, ",")); H.eq(status, "failed")
    end
    H.eq(#seen, n, "none of them reached the DLL")
    package.loadlib = real_loadlib
    _G.TurboPlayerMove = nil
end)

H.case("no unmapped memory reads", function() H.eq(sim.unmapped_reads, 0) end)

H.finish()
