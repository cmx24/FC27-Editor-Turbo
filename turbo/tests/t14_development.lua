package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t14 development, reveal player data, youth academy (Manager Career)")

local sim = H.setup({ in_cm = true })
W.build(sim, { development = true })

local function run(module, over) return H.turbo().run(module, over) end
local function pval(pid, field) return sim:value("players", sim:find_row("players", "playerid", pid), field) end
local function attr(pid, a) return pval(pid, a) end

local USER = W.USER_PLAYERS[3]      -- 1003: user team, ovr 63, pot 73, position 4
local OTHER = 2001                  -- team 2, ovr 56, pot 66, position 1
local GK = W.USER_PLAYERS[1]        -- 1001: the user's goalkeeper (position 0), ovr 61, pot 71

-- ---------------------------------------------------------------- development
H.case("development: validation (scope, mode, confirm, ranges)", function()
    local ok, msg = run("development", { scope = {}, mode = "to_potential" })
    H.eq(ok, false); H.has(msg, "scope must be")
    ok, msg = run("development", { scope = { playerid = 999998 }, mode = "to_potential" })
    H.eq(ok, false); H.has(msg, "not in the players table")
    ok, msg = run("development", { scope = { playerid = USER }, mode = "fast" })
    H.eq(ok, false); H.has(msg, "mode must be")
    ok, msg = run("development", { scope = { playerid = USER }, mode = "add", delta = 0 })
    H.eq(ok, false); H.has(msg, "delta")
    ok, msg = run("development", { scope = { playerid = USER }, mode = "set", attributes = { finishing = 120 } })
    H.eq(ok, false); H.has(msg, "1..99")
    ok, msg = run("development", { scope = { playerid = USER }, mode = "set", attributes = { flying = 50 } })
    H.eq(ok, false); H.has(msg, "unknown attribute")
    ok, msg = run("development", { scope = { playerid = USER }, mode = "none", potential = 140 })
    H.eq(ok, false); H.has(msg, "potential must be 1..99")
    ok, msg = run("development", { scope = { user_team = true }, mode = "add", delta = 1 })
    H.eq(ok, false); H.has(msg, "confirm")
    H.eq(attr(USER, "finishing"), 40 + USER % 30, "nothing written by a refused run")
    sim.in_cm = false
    ok, msg = run("development", { scope = { playerid = USER }, mode = "to_potential" })
    H.eq(ok, false); H.has(msg, "career")
    sim.in_cm = true
end)

H.case("development: to potential grows the group attributes and the overall; your player's plan follows", function()
    local before = attr(USER, "finishing")
    local gk_before = attr(USER, "gkdiving")
    sim.calls.PlayerSetValueInDevelopementPlan = nil
    local ok, msg = run("development", { scope = { playerid = USER }, mode = "to_potential" })
    H.eq(ok, true, msg); H.has(msg, "1 players changed"); H.has(msg, "1 development plans updated")
    H.eq(pval(USER, "overallrating"), 73, "overall = potential")
    H.eq(attr(USER, "finishing"), math.min(99, before + 10), "finishing +gap")
    H.eq(attr(USER, "gkdiving"), gk_before, "goalkeeping untouched for an outfielder")
    H.eq(sim.dev_plans[USER].finishing, attr(USER, "finishing"), "plan holds the new value")
    H.eq(sim.dev_plans[USER].gkdiving, nil, "plan: only changed attributes")
    -- already at potential: nothing to do
    ok, msg = run("development", { scope = { playerid = USER }, mode = "to_potential" })
    H.eq(ok, true, msg); H.has(msg, "0 players changed")
end)

H.case("development: a goalkeeper grows goalkeeping, another club's player has no plan", function()
    local ok, msg = run("development", { scope = { playerid = GK }, mode = "add", delta = 2 })
    H.eq(ok, true, msg)
    H.eq(attr(GK, "gkdiving"), 40 + GK % 30 + 2); H.eq(attr(GK, "finishing"), 40 + GK % 30, "outfield untouched")
    H.eq(pval(GK, "overallrating"), 63, "overall +2")
    ok, msg = run("development", { scope = { playerid = OTHER }, mode = "set", attributes = { finishing = 77, vision = 70 },
        potential = 85, growthprofile = 5 })
    H.eq(ok, true, msg); H.eq(msg:find("development plans updated", 1, true), nil, "no plan for another club's player")
    H.eq(attr(OTHER, "finishing"), 77); H.eq(attr(OTHER, "vision"), 70)
    H.eq(pval(OTHER, "potential"), 85); H.eq(pval(OTHER, "growthprofile"), 5)
    H.eq(sim.dev_plans[OTHER], nil)
end)

H.case("development: a whole club with confirm; dry run writes nothing", function()
    H.write_config({ turbo = { dry_run = true } })
    local v = attr(2002, "finishing")
    local ok, msg = run("development", { scope = { teamid = 2 }, mode = "add", delta = 3, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]"); H.eq(attr(2002, "finishing"), v, "dry run")
    H.write_config({ turbo = { dry_run = false } })
    ok, msg = run("development", { scope = { teamid = 2 }, mode = "add", delta = 3, attributes = { "finishing" }, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "4 players changed")
    H.eq(attr(2002, "finishing"), v + 3)
    H.eq(pval(2002, "overallrating"), 57, "a single attribute does not move the overall")
end)

H.case("development auto: weekly forced growth up to potential, no decline, snapshot on load", function()
    local dev = require 'imports/turbo/features/development'
    local pid = W.USER_PLAYERS[10]   -- ovr 70, pot 80
    local cfg = { players = { pid }, weekly = 2, no_decline = true }
    local ctx = { cfg = cfg, dry = false }
    dev.auto(ctx, -1)   -- armed: snapshot only, nobody grows
    H.eq(pval(pid, "overallrating"), 70, "arming grows nobody")
    local week = require('imports/turbo/core/events').resolve("WEEK_PASSED")
    local base = attr(pid, "finishing")
    dev.auto(ctx, week)
    H.eq(attr(pid, "finishing"), base + 2); H.eq(pval(pid, "overallrating"), 72)
    -- age decline by the game: put back on the next week, then grows again
    local tbl = require('imports/turbo/core/db').get_table("players")
    local rec = require('imports/turbo/core/db').find(tbl, "playerid", pid)
    tbl:SetRecordFieldValue(rec, "finishing", base - 5)
    tbl:SetRecordFieldValue(rec, "gkdiving", 1)
    dev.auto(ctx, week)
    H.eq(attr(pid, "finishing"), base + 4, "restored then grown")
    H.ok(attr(pid, "gkdiving") > 1, "goalkeeping drop put back")
    for _ = 1, 10 do dev.auto(ctx, week) end
    H.eq(pval(pid, "overallrating"), 80, "stops at potential")
    -- a loaded career: snapshot starts over, a drop there is not restored from the old one
    dev.auto(ctx, require('imports/turbo/core/events').resolve("POST_LOAD_PREPARE"))
    tbl:SetRecordFieldValue(rec, "vision", 2)
    dev.auto(ctx, require('imports/turbo/core/events').resolve("POST_LOAD_PREPARE"))
    dev.auto(ctx, week)
    H.eq(attr(pid, "vision"), 2, "snapshot of the newly loaded career")
    local _, _, err = dev.weekly({ players = { pid }, weekly = 9 })
    H.has(err, "weekly must be")
end)

H.case("development: caps (plan natives) and the boot arms the weekly listener", function()
    local caps = require 'imports/turbo/core/caps'
    H.eq(caps.unavailable().development, nil, "development available with the plan natives")
    H.write_config({ auto = { development = { enabled = true, players = { 1005 }, weekly = 1 } } })
    H.turbo().boot()
    H.ok(TURBO_STATE.listeners.development ~= nil, "weekly listener registered")
    H.write_config({ auto = { development = { enabled = false } } })
    H.turbo().boot()
    H.eq(TURBO_STATE.listeners.development, nil)
    local has = _G.PlayerHasDevelopementPlan
    _G.PlayerHasDevelopementPlan = nil
    H.ok(caps.unavailable().development ~= nil, "greyed without the plan natives")
    local ok, msg = run("development", { scope = { playerid = 2003 }, mode = "add", delta = 1 })
    H.eq(ok, true, msg); H.has(msg, "no development-plan natives")
    _G.PlayerHasDevelopementPlan = has
end)

-- ---------------------------------------------------------------- reveal
H.case("reveal: validation and the native gate", function()
    local ok, msg = run("reveal", { scope = { playerid = 999998 } })
    H.eq(ok, false); H.has(msg, "not in the players table")
    ok, msg = run("reveal", { scope = { teamid = 424242 } })
    H.eq(ok, false); H.has(msg, "not in the teams table")
    ok, msg = run("reveal", { scope = { leagueid = 99 } })
    H.eq(ok, false); H.has(msg, "no teams")
    ok, msg = run("reveal", { scope = { leagueid = 13 } })
    H.eq(ok, false); H.has(msg, "confirm"); H.has(msg, "league 13 (7 clubs)")
    ok, msg = run("reveal", { scope = { nothing = true } })
    H.eq(ok, false); H.has(msg, "scope must be")
    ok, msg = run("reveal", { scope = { playerid = OTHER } })
    H.eq(ok, false); H.has(msg, "not available")
    local caps = require 'imports/turbo/core/caps'
    H.ok(caps.unavailable().reveal ~= nil, "reveal greyed until Turbo.dll's native is there")
end)

H.case("reveal: player, club, league through the native with the PlayerDataRevealManager", function()
    local pdrm = sim:add_manager(78, 0x7D0)
    -- a record vector with 2 records and room for 1500
    local recs = sim:alloc(1500 * 0x14, 8)
    sim:w64(pdrm + 0x790, recs); sim:w64(pdrm + 0x798, recs + 2 * 0x14); sim:w64(pdrm + 0x7A0, recs + 1500 * 0x14)
    local calls = {}
    _G.TurboRevealPlayerData = function(addr, mode, id)
        calls[#calls + 1] = { addr, mode, id }
        return true, string.format("%s %d: data revealed fully", mode == 1 and "team" or "player", id), "ok", 204, 2
    end
    local ok, msg = run("reveal", { scope = { playerid = OTHER } })
    H.eq(ok, true, msg); H.has(msg, "player 2001: data revealed fully"); H.has(msg, "reveal records 2 -> 2")
    H.eq(#calls, 1); H.eq(calls[1][1], pdrm, "manager passed"); H.eq(calls[1][2], 0); H.eq(calls[1][3], OTHER)
    ok, msg = run("reveal", { scope = { user_team = true } })
    H.eq(ok, true, msg); H.has(msg, "your club"); H.eq(calls[2][2], 1); H.eq(calls[2][3], W.USER_TEAM)
    calls = {}
    ok, msg = run("reveal", { scope = { leagueid = 31 }, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "7 game calls"); H.eq(#calls, 7)
    H.eq(calls[1][3], 8); H.eq(calls[7][3], 14)
    -- the 1500-record limit: refused unless allowed
    sim:w64(pdrm + 0x798, recs + 1496 * 0x14)
    ok, msg = run("reveal", { scope = { teamid = 2 } })
    H.eq(ok, false); H.has(msg, "1496 reveal records"); H.has(msg, "allow_evict")
    ok, msg = run("reveal", { scope = { teamid = 2 }, allow_evict = true })
    H.eq(ok, true, msg)
    sim:w64(pdrm + 0x798, recs + 2 * 0x14)
    -- queued (off the game thread): stops after the first, says what is left
    calls = {}
    _G.TurboRevealPlayerData = function(addr, mode, id)
        calls[#calls + 1] = { addr, mode, id }
        return true, "queued for the game thread: it runs at the next game tick", "queued"
    end
    ok, msg = run("reveal", { scope = { teamids = { 2, 3, 4 } }, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "queued"); H.has(msg, "2 more to go"); H.eq(#calls, 1)
    -- failed in the DLL
    _G.TurboRevealPlayerData = function() return false, "PlayerDataRevealManager mismatch", "failed" end
    ok, msg = run("reveal", { scope = { playerid = OTHER } })
    H.eq(ok, false); H.has(msg, "mismatch")
    _G.TurboRevealPlayerData = nil
end)

H.case("reveal: the bridge native carries manager, mode, id and the manager table to Turbo.dll (op 3)", function()
    local bridge = require 'imports/turbo/bridge'
    TURBO_STATE.bridge.next_dll_check = 0
    local mb = bridge.mailbox_address()
    H.ok(mb ~= nil, "mailbox")
    local OP, STATUS, SEQ, RSEQ, ARGS, OUT, TEXT = 0x2020, 0x2024, 0x2028, 0x202C, 0x2030, 0x2050, 0x2060
    local got = {}
    local function fake_call()
        local seq = sim:r32(mb + SEQ)
        got[#got + 1] = { op = sim:r32(mb + OP), a1 = sim:r64(mb + ARGS), a2 = sim:r64(mb + ARGS + 8), a3 = sim:r64(mb + ARGS + 16),
                          a4 = sim:r64(mb + ARGS + 24) }
        sim:wstr(mb + TEXT, "player 2001: data revealed fully (scouting points 204/204, dated 20270115, no record before)")
        sim:w64(mb + OUT, 204); sim:w64(mb + OUT + 8, 2)
        sim:w32(mb + RSEQ, seq); sim:w32(mb + STATUS, 1)
    end
    local real = package.loadlib
    package.loadlib = function(_, sym)
        if sym == "turbo_game_call" then return fake_call end
        if sym == "turbo_game_pump" then return function() end end
        return nil
    end
    bridge.install_natives()
    H.eq(type(_G.TurboRevealPlayerData), "function")
    H.eq(bridge.CALL_OP_REVEAL, 3)
    local pdrm = sim.managers[78]
    local ok, text, status, o0, o1 = _G.TurboRevealPlayerData(pdrm, 0, OTHER)
    H.eq(ok, true, text); H.eq(status, "ok"); H.has(text, "204/204"); H.eq(o0, 204); H.eq(o1, 2)
    H.eq(got[1].op, 3); H.eq(got[1].a1, pdrm); H.eq(got[1].a2, 0); H.eq(got[1].a3, OTHER); H.eq(got[1].a4, sim.mode_managers)
    local caps = require 'imports/turbo/core/caps'
    H.eq(caps.unavailable().reveal, nil, "reveal available")
    local okr, msg = run("reveal", { scope = { teamid = 7 } })
    H.eq(okr, true, msg); H.eq(got[2].a2, 1); H.eq(got[2].a3, 7)
    package.loadlib = real
    _G.TurboRevealPlayerData, _G.TurboJobOfferCreate, _G.TurboStandingsRefresh = nil, nil, nil
end)

-- ---------------------------------------------------------------- youth academy
H.case("youth: list the academy and set potential / position / tier / variance", function()
    local ok, msg = run("youth", { mode = "list" })
    H.eq(ok, true, msg); H.has(msg, "3 academy players")
    local lines = H.csv_lines(H.out("youth_academy.csv"))
    H.eq(#lines, 4, "header + 3"); H.has(lines[2], "460001,")
    ok, msg = run("youth", { mode = "set", playerid = 2001, potential = 90 })
    H.eq(ok, false); H.has(msg, "not in the youth academy")
    ok, msg = run("youth", { mode = "set", playerid = 460002 })
    H.eq(ok, false); H.has(msg, "nothing to change")
    ok, msg = run("youth", { mode = "set", playerid = 460002, potential = 100 })
    H.eq(ok, false); H.has(msg, "1..99")
    ok, msg = run("youth", { mode = "set", playerid = 460002, tier = 9 })
    H.eq(ok, false); H.has(msg, "outside the field range")
    ok, msg = run("youth", { mode = "set", playerid = 460002, potential = 92, position = 25, tier = 3, variance = 0 })
    H.eq(ok, true, msg); H.has(msg, "potential=92")
    H.eq(pval(460002, "potential"), 92); H.eq(pval(460002, "preferredposition1"), 25)
    local yrec = sim:find_row("career_youthplayers", "playerid", 460002)
    H.eq(sim:value("career_youthplayers", yrec, "playertier"), 3)
    H.eq(sim:value("career_youthplayers", yrec, "potentialvariance"), 0)
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
