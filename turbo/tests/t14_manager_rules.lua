package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t14 manager rules (job security, unsackable) and manager market (feature flags, validation, natives)")

local sim = H.setup({ in_cm = true })
W.build(sim, {})
-- the career database's manager table and a national team (teamnationlinks), as FC 27 names them
local MGR = { -- managerid, first, surname, teamid (0 = free agent)
    { 501, "Mikel", "Arteta", W.USER_TEAM }, { 502, "David", "Moyes", 7 }, { 503, "Cristian", "Chivu", 241 },
    { 504, "Thomas", "Tuchel", 14 }, { 505, "Free", "Agent", 0 }, { 506, "Other", "Free", 0 }, { 507, "Ange", "P", 8 },
}
local mrows = {}
for _, m in ipairs(MGR) do mrows[#mrows + 1] = { managerid = m[1], firstname = m[2], surname = m[3], teamid = m[4] } end
sim:add_table({
    name = "manager", short = "mngr",
    fields = {
        { name = "managerid", short = "mid_", depth = 16 },
        { name = "firstname", short = "fnam", type = "string", depth = 8 * 20 },
        { name = "surname", short = "snam", type = "string", depth = 8 * 20 },
        { name = "teamid", short = "tid_", depth = 18 },
    },
    rows = mrows,
})
sim:add_table({
    name = "teamnationlinks", short = "tnli",
    fields = { { name = "teamid", short = "tid_", depth = 18 }, { name = "nationid", short = "nid_", depth = 9 } },
    rows = { { teamid = 14, nationid = 14 } },
})

local function run(name, over) return H.turbo().run(name, over) end

local function manager_team(mid)
    local db = require 'imports/turbo/core/db'
    local t = db.get_table("manager")
    local rec = db.find(t, "managerid", mid)
    return rec and db.get(t, rec, "teamid")
end

H.case("off by default: both feature flags refuse before anything else", function()
    local ok, msg = run("manager_rules", { job_security = "safe", confirm = true })
    H.eq(ok, false); H.has(msg, "feature flag")
    ok, msg = run("manager_move", { managerid = 502, teamid = 8, confirm = true })
    H.eq(ok, false); H.has(msg, "feature flag")
end)

H.case("manager rules: validation and dry run", function()
    local mr = require 'imports/turbo/features/manager_rules'
    H.eq(mr.parse_job_security("Very-Insecure").level, 0); H.eq(mr.parse_job_security("safe").level, 3)
    H.eq(mr.parse_job_security("OK").level, 2); H.eq(mr.parse_job_security(45).score, 45)
    H.eq(mr.parse_job_security("game").restore, true); H.eq(mr.parse_job_security("sacked"), nil)
    local ok, msg = run("manager_rules", { enabled = true, job_security = "sacked", confirm = true })
    H.eq(ok, false); H.has(msg, "job_security must be")
    ok, msg = run("manager_rules", { enabled = true, job_security = 150, confirm = true })
    H.eq(ok, false); H.has(msg, "0..100")
    ok, msg = run("manager_rules", { enabled = true, unsackable = "yes", confirm = true })
    H.eq(ok, false); H.has(msg, "true or false")
    ok, msg = run("manager_rules", { enabled = true, confirm = true })
    H.eq(ok, false); H.has(msg, "nothing to do")
    ok, msg = run("manager_rules", { enabled = true, job_security = "safe" })
    H.eq(ok, false); H.has(msg, "confirm")
    sim.in_cm = false
    ok, msg = run("manager_rules", { enabled = true, job_security = "safe", confirm = true })
    sim.in_cm = true
    H.eq(ok, false); H.has(msg, "career")
    H.write_config({ turbo = { dry_run = true } })
    ok, msg = run("manager_rules", { enabled = true, job_security = "safe", unsackable = true, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]"); H.has(msg, "unsackable on")
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("manager rules: without Turbo.dll's native the run is refused and the GUI sees the cap", function()
    local ok, msg = run("manager_rules", { enabled = true, job_security = "safe", confirm = true })
    H.eq(ok, false); H.has(msg, "not available")
    local caps = require 'imports/turbo/core/caps'
    H.ok(caps.unavailable().manager_rules ~= nil, "manager_rules listed as unavailable")
end)

H.case("manager rules: the native gets the right sub-op, manager and team; keep file; state for the window", function()
    local mr = require 'imports/turbo/features/manager_rules'
    local calls = {}
    local answer = { true, "job security 55 -> 100 (locked safe)", "ok", 100, 100 }
    _G.TurboManagerRules = function(sub, addr, value, team)
        calls[#calls + 1] = { sub = sub, addr = addr, value = value, team = team }
        return table.unpack(answer)
    end
    local com = sim:add_manager(133, 0x300)
    local jsm = sim:add_manager(54, 0x200)
    -- like the game's constructors: +0x8 = the career manager table the object is registered in
    sim:w64(com + 0x08, sim.mode_managers); sim:w64(jsm + 0x08, sim.mode_managers)
    local ok, msg = run("manager_rules", { enabled = true, job_security = "safe", confirm = true })
    H.eq(ok, true, msg); H.has(msg, "locked safe")
    H.eq(#calls, 1); H.eq(calls[1].sub, mr.SUB_SET_LEVEL); H.eq(calls[1].value, 3); H.eq(calls[1].addr, com, "ClubObjectivesManager passed")
    H.eq(calls[1].team, W.USER_TEAM, "your club passed for the DLL's check")
    run("manager_rules", { enabled = true, job_security = "okay", confirm = true })
    H.eq(calls[2].sub, mr.SUB_SET_LEVEL); H.eq(calls[2].value, 2)
    run("manager_rules", { enabled = true, job_security = 45, confirm = true })
    H.eq(calls[3].sub, mr.SUB_SET_SCORE); H.eq(calls[3].value, 45)
    run("manager_rules", { enabled = true, job_security = "game", confirm = true })
    H.eq(calls[4].sub, mr.SUB_RESTORE)
    -- unsackable: the JobSwitchManager goes to the DLL; "keep" lands in turbo_output\manager_rules_keep.json
    answer = { true, "unsackable on: the game's SackManager is refused", "ok", 1, 0 }
    ok, msg = run("manager_rules", { enabled = true, unsackable = true, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "unsackable on"); H.has(msg, "kept")
    H.eq(calls[5].sub, mr.SUB_UNSACKABLE); H.eq(calls[5].value, 1); H.eq(calls[5].addr, jsm, "JobSwitchManager passed")
    local keep = H.read(H.LE .. "/turbo_output/manager_rules_keep.json")
    H.ok(keep and keep:find('"unsackable":true', 1, true), "keep file: " .. tostring(keep))
    answer = { true, "unsackable off: the game may sack you again", "ok", 0, 0 }
    ok, msg = run("manager_rules", { enabled = true, unsackable = false, confirm = true })
    H.eq(ok, true, msg); H.eq(calls[6].value, 0)
    keep = H.read(H.LE .. "/turbo_output/manager_rules_keep.json")
    H.ok(keep and keep:find('"unsackable":false', 1, true), "off is remembered too: " .. tostring(keep))
    -- the DLL refuses: the reason comes back
    answer = { false, "the object at 0x1 is not the ClubObjectivesManager", "failed" }
    ok, msg = run("manager_rules", { enabled = true, job_security = "safe", confirm = true })
    H.eq(ok, false); H.has(msg, "not the ClubObjectivesManager")

    -- the window's view (bridge_state.json manager_rules): only a ClubObjectivesManager whose block points back at it
    sim:w64(com + 0x108, com); sim:w8(com + 0x110, 1); sim:w32(com + 0x114, W.USER_TEAM)
    sim:w32(com + 0x118, 100); sim:w32(com + 0x124, 100)
    sim:w32(com + 0x27C, 30); sim:w32(com + 0x280, 50); sim:w32(com + 0x284, 70)
    sim:w8(jsm + 0x1E0, 1)
    local bridge = require 'imports/turbo/bridge'
    local st = bridge.collect_state().manager_rules
    H.ok(st ~= nil, "state published")
    H.eq(st.score, 100); H.eq(st.addon, 100); H.eq(st.level, "safe"); H.eq(st.locked, "safe")
    H.eq(st.insecure, 30); H.eq(st.okay, 50); H.eq(st.safe, 70)
    H.eq(st.sack_pending, true); H.eq(st.sacked, false); H.eq(st.keep_unsackable, false)
    sim:w32(com + 0x118, -100); sim:w32(com + 0x124, 42)
    st = bridge.collect_state().manager_rules
    H.eq(st.level, "insecure"); H.eq(st.locked, "very insecure"); H.eq(st.addon, -100, "negative addon read signed")
    sim:w64(com + 0x108, com + 8)   -- not the manager's own block: the score is not trusted
    st = bridge.collect_state().manager_rules
    H.eq(st.score, nil, "score not read from a foreign layout")
    sim:w64(com + 0x108, com)
    sim.in_cm = false
    H.eq(bridge.collect_state().manager_rules, nil, "nothing outside a career")
    sim.in_cm = true
    _G.TurboManagerRules = nil
end)

-- the live game of 04-10-2026: slot 133 / 54 hold the managers and each points back at the table from +0x8; an object
-- that does not is never passed to Turbo.dll nor read for the window
H.case("manager rules: locate needs the slot's object to point back at the manager table (+0x8)", function()
    local mr = require 'imports/turbo/features/manager_rules'
    local bridge = require 'imports/turbo/bridge'
    local com, jsm = sim.managers[133], sim.managers[54]
    H.eq(mr.locate(133), com, "ClubObjectivesManager"); H.eq(mr.locate(54), jsm, "JobSwitchManager")
    local calls = {}
    _G.TurboManagerRules = function(sub, addr, value, team)
        calls[#calls + 1] = { sub = sub, addr = addr }
        return true, "done", "ok", 0, 0
    end
    sim:w64(com + 0x08, com)   -- not the table
    local a, why = mr.locate(133)
    H.eq(a, nil); H.has(why, "does not point back")
    local ok, msg = run("manager_rules", { enabled = true, job_security = "insecure", confirm = true })
    H.eq(ok, false); H.has(msg, "does not point back"); H.eq(#calls, 0, "the DLL is not asked with an unproven object")
    H.eq(bridge.collect_state().manager_rules.score, nil, "the window does not read it either")
    sim:w64(com + 0x08, sim.mode_managers)
    ok, msg = run("manager_rules", { enabled = true, job_security = "insecure", confirm = true })
    H.eq(ok, true, msg); H.eq(calls[1].sub, mr.SUB_SET_LEVEL); H.eq(calls[1].addr, com)
    -- a JobSwitchManager that does not point back: 0 goes to the DLL (it uses the one its HandleEvent hook saw)
    sim:w64(jsm + 0x08, 0)
    H.eq(mr.locate(54), nil)
    ok, msg = run("manager_rules", { enabled = true, unsackable = true, keep = false, confirm = true })
    H.eq(ok, true, msg); H.eq(calls[2].sub, mr.SUB_UNSACKABLE); H.eq(calls[2].addr, 0, "no unproven JobSwitchManager passed")
    H.eq(bridge.collect_state().manager_rules.sack_pending, nil, "flags not read from it")
    sim:w64(jsm + 0x08, sim.mode_managers)
    _G.TurboManagerRules = nil
end)

H.case("manager rules: a kept unsackable is switched on again once per game session", function()
    local mr = require 'imports/turbo/features/manager_rules'
    mr.reset_cache()
    local f = assert(io.open(H.LE .. "/turbo_output/manager_rules_keep.json", "wb"))
    f:write('{"unsackable":true}'); f:close()
    H.eq(mr.reapply(), false, "no native yet: nothing (tried again later)")
    local calls = {}
    _G.TurboManagerRules = function(sub, addr, value, team)
        calls[#calls + 1] = { sub = sub, value = value }
        return true, "unsackable on", "ok", 1, 0
    end
    H.eq(mr.reapply(), true, "applied once the native is there")
    H.eq(#calls, 1); H.eq(calls[1].sub, mr.SUB_UNSACKABLE); H.eq(calls[1].value, 1)
    H.eq(mr.reapply(), false, "not twice in a session")
    H.eq(mr.state().keep_unsackable, true, "the window sees the kept switch")
    mr.reset_cache()
    f = assert(io.open(H.LE .. "/turbo_output/manager_rules_keep.json", "wb"))
    f:write('{"unsackable":false}'); f:close()
    H.eq(mr.reapply(), false, "kept off: nothing to do")
    H.eq(#calls, 1)
    _G.TurboManagerRules = nil
end)

-- TurboManagerRules is defined by bridge.install_natives on top of Turbo.dll's turbo_game_call: op 4, four arguments
H.case("bridge: TurboManagerRules travels as game call op 4 (sub-op, address, value, team) and returns the outputs", function()
    local bridge = require 'imports/turbo/bridge'
    TURBO_STATE.bridge.next_dll_check = 0
    local mb = bridge.mailbox_address()
    H.ok(mb ~= nil, "mailbox")
    local OP, STATUS, SEQ, RSEQ, ARGS, OUT, TEXT = 0x2020, 0x2024, 0x2028, 0x202C, 0x2030, 0x2050, 0x2060
    local seen = {}
    local function fake_turbo_game_call()
        local seq = sim:r32(mb + SEQ)
        seen[#seen + 1] = { op = sim:r32(mb + OP), a = { sim:r64(mb + ARGS), sim:r64(mb + ARGS + 8), sim:r64(mb + ARGS + 16), sim:r64(mb + ARGS + 24) } }
        sim:wstr(mb + TEXT, "job security 55 -> 100 (locked safe)")
        sim:w64(mb + OUT, 100); sim:w64(mb + OUT + 8, 100)
        sim:w32(mb + RSEQ, seq); sim:w32(mb + STATUS, 1)
    end
    local real = package.loadlib
    package.loadlib = function(_, sym)
        if sym == "turbo_game_call" then return fake_turbo_game_call end
        if sym == "turbo_game_pump" then return function() end end
        return nil
    end
    H.eq(bridge.install_natives(), true)
    H.eq(type(_G.TurboManagerRules), "function", "TurboManagerRules defined")
    local caps = require 'imports/turbo/core/caps'
    H.eq(caps.unavailable().manager_rules, nil, "manager_rules now available")
    local ok, text, status, o0, o1 = _G.TurboManagerRules(2, 0x1234, 3, 1)
    H.eq(ok, true, text); H.eq(status, "ok"); H.eq(o0, 100); H.eq(o1, 100); H.has(text, "locked safe")
    H.eq(seen[1].op, 4, "op manager_rules"); H.eq(seen[1].a[1], 2); H.eq(seen[1].a[2], 0x1234); H.eq(seen[1].a[3], 3); H.eq(seen[1].a[4], 1)
    -- the feature end to end through the mailbox
    local ok2, msg = run("manager_rules", { enabled = true, job_security = "safe", confirm = true })
    H.eq(ok2, true, msg); H.eq(seen[2].a[2], sim.managers[133], "the career's ClubObjectivesManager")
    package.loadlib = real
    _G.TurboManagerRules, _G.TurboJobOfferCreate, _G.TurboStandingsRefresh = nil, nil, nil
end)

H.case("manager market: validation (own club, national team, unknown ids, confirmation) and dry run", function()
    local ok, msg = run("manager_move", { enabled = true, managerid = 0, teamid = 8, confirm = true })
    H.eq(ok, false); H.has(msg, "positive")
    ok, msg = run("manager_move", { enabled = true, managerid = 999, teamid = 8, confirm = true })
    H.eq(ok, false); H.has(msg, "not in the manager table")
    ok, msg = run("manager_move", { enabled = true, managerid = 501, teamid = 8, confirm = true })
    H.eq(ok, false); H.has(msg, "your club")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = W.USER_TEAM, confirm = true })
    H.eq(ok, false); H.has(msg, "your own club")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 14, confirm = true })
    H.eq(ok, false); H.has(msg, "national team")
    ok, msg = run("manager_move", { enabled = true, managerid = 504, teamid = 8, confirm = true })
    H.eq(ok, false); H.has(msg, "national team")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 424242, confirm = true })
    H.eq(ok, false); H.has(msg, "not in the teams table")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 7, confirm = true })
    H.eq(ok, false); H.has(msg, "already manages")
    ok, msg = run("manager_move", { enabled = true, managerid = 505, teamid = 0, confirm = true })
    H.eq(ok, false); H.has(msg, "already a free agent")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 0, replacement = 503, confirm = true })
    H.eq(ok, false); H.has(msg, "not a free agent")
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 8 })
    H.eq(ok, false); H.has(msg, "confirm"); H.has(msg, "Moyes")
    H.write_config({ turbo = { dry_run = true } })
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 8, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]")
    H.eq(manager_team(502), 7, "dry run wrote nothing"); H.eq(manager_team(507), 8)
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("manager market: swap two club managers, a free agent takes a club, a manager made available", function()
    -- Moyes (Everton 7) to team 8: Ange (team 8) takes Everton
    local ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 8, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "moved")
    H.eq(manager_team(502), 8); H.eq(manager_team(507), 7)
    -- a free agent (505) to Inter (241): Chivu becomes a free agent, every club keeps one manager
    ok, msg = run("manager_move", { enabled = true, managerid = 505, teamid = 241, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "free agent")
    H.eq(manager_team(505), 241); H.eq(manager_team(503), 0)
    -- Moyes made available: the first free agent (503 or 506) takes team 8
    ok, msg = run("manager_move", { enabled = true, managerid = 502, teamid = 0, confirm = true })
    H.eq(ok, true, msg)
    H.eq(manager_team(502), 0)
    local taker = (manager_team(503) == 8) and 503 or 506
    H.eq(manager_team(taker), 8, "a free agent took team 8")
    -- a chosen replacement
    ok, msg = run("manager_move", { enabled = true, managerid = 505, teamid = 0, replacement = 502, confirm = true })
    H.eq(ok, true, msg); H.eq(manager_team(505), 0); H.eq(manager_team(502), 241)
    -- no free agent left for a release
    local empty = { 9, 10, 11 }   -- clubs without a manager row: a free agent simply takes them
    for _, mid in ipairs({ 503, 505, 506 }) do
        if manager_team(mid) == 0 then
            local okm, mmsg = run("manager_move", { enabled = true, managerid = mid, teamid = table.remove(empty, 1), confirm = true })
            H.eq(okm, true, mmsg)
        end
    end
    ok, msg = run("manager_move", { enabled = true, managerid = 507, teamid = 0, confirm = true })
    H.eq(ok, false); H.has(msg, "no free-agent manager")
    H.eq(manager_team(501), W.USER_TEAM, "your club's manager never moved")
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
