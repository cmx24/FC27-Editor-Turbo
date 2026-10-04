package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t12 job offer (feature flag, validation, native gate)")

local sim = H.setup({ in_cm = true })
W.build(sim, {})

local function run(over) return H.turbo().run("job_offer", over) end

H.case("off by default: the feature flag refuses before anything else", function()
    local ok, msg = run({ teamid = 7, confirm = true })
    H.eq(ok, false); H.has(msg, "feature flag")
end)

H.case("validation: team id, unknown team, national team, own club, confirmation", function()
    local ok, msg = run({ enabled = true, teamid = 0, confirm = true })
    H.eq(ok, false); H.has(msg, "positive whole number")
    ok, msg = run({ enabled = true, teamid = 999999, confirm = true })
    H.eq(ok, false); H.has(msg, "not in the teams table")
    local nat = sim.tables.teamnationlinks and sim.tables.teamnationlinks.rows[1]
    if nat then
        ok, msg = run({ enabled = true, teamid = nat.teamid, confirm = true })
        H.eq(ok, false); H.has(msg, "national team")
    end
    ok, msg = run({ enabled = true, teamid = W.USER_TEAM, confirm = true })
    H.eq(ok, false); H.has(msg, "own club")
    ok, msg = run({ enabled = true, teamid = 7 })
    H.eq(ok, false); H.has(msg, "confirm")
end)

H.case("dry run reports the plan and touches nothing", function()
    H.write_config({ turbo = { dry_run = true } })
    local before = {}
    for k, v in pairs(sim.mem) do before[k] = v end
    local ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "[DRY RUN]"); H.has(msg, "Everton")
    local changed = 0
    for k, v in pairs(sim.mem) do if before[k] ~= v then changed = changed + 1 end end
    H.eq(changed, 0, "bytes changed in dry run")
    H.write_config({ turbo = { dry_run = false } })
end)

H.case("without the Turbo.dll native the run is refused and the GUI sees the cap", function()
    local ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, false); H.has(msg, "not available")
    local caps = require 'imports/turbo/core/caps'
    H.ok(caps.unavailable().job_offer ~= nil, "job_offer listed as unavailable")
end)

H.case("with the native: refuses outside a career and without the memory map, calls it with the manager", function()
    local calls = {}
    _G.TurboJobOfferCreate = function(addr, tid) calls[#calls + 1] = { addr, tid }; return true end
    sim.in_cm = false
    local ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, false); H.has(msg, "career")
    sim.in_cm = true
    ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    local mem = require 'imports/turbo/core/mem'
    if not mem.map_available() then
        H.eq(ok, false, msg); H.has(msg, "Turbo GUI"); H.eq(#calls, 0, "native not called without the map")
    elseif not mem.manager(53) then
        H.eq(ok, false, msg); H.has(msg, "JobMarketManager"); H.eq(#calls, 0, "native not called without the manager")
    else
        H.eq(ok, true, msg); H.eq(#calls, 1, "native called once"); H.eq(calls[1][2], 7, "team id passed")
    end
    _G.TurboJobOfferCreate = function() return false, "no application node" end
    ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, false)
    _G.TurboJobOfferCreate = nil
end)

-- The real native: bridge.install_natives defines TurboJobOfferCreate on top of Turbo.dll's turbo_game_call export,
-- and the arguments / result travel through the mailbox call block (+0x2020). A fake Turbo.dll answers here.
H.case("bridge game call: the mailbox call block carries op + args to Turbo.dll and status / text / outputs back", function()
    local bridge = require 'imports/turbo/bridge'
    TURBO_STATE.bridge.next_dll_check = 0
    local mb = bridge.mailbox_address()
    H.ok(mb ~= nil, "mailbox found through bridge_dll.json")
    H.eq(sim:r32(mb + 4), 2, "mailbox version 2 (call block)")
    local OP, STATUS, SEQ, RSEQ, ARGS, OUT, TEXT = 0x2020, 0x2024, 0x2028, 0x202C, 0x2030, 0x2050, 0x2060
    local dll = { calls = {}, answer = { status = 1, text = "job offer sent on 20270115 (weekly wage 42000)", out0 = 20270115, out1 = 42000 } }
    local function fake_turbo_game_call()
        local seq = sim:r32(mb + SEQ)
        dll.calls[#dll.calls + 1] = { op = sim:r32(mb + OP), seq = seq, jmm = sim:r64(mb + ARGS), team = sim:r64(mb + ARGS + 8),
                                      a3 = sim:r64(mb + ARGS + 16), a4 = sim:r64(mb + ARGS + 24) }
        local a = dll.answer
        sim:wstr(mb + TEXT, a.text)
        sim:w64(mb + OUT, a.out0 or 0)
        sim:w64(mb + OUT + 8, a.out1 or 0)
        sim:w32(mb + RSEQ, a.rseq or seq)
        sim:w32(mb + STATUS, a.status)
    end
    local real_loadlib = package.loadlib
    package.loadlib = function(path, sym)
        if sym == "turbo_game_call" then return fake_turbo_game_call end
        if sym == "turbo_game_pump" then return function() end end
        return nil
    end
    H.eq(bridge.install_natives(), true, "natives installed once the export is found")
    H.eq(bridge.install_natives(), false, "second call: nothing new")
    H.eq(type(_G.TurboJobOfferCreate), "function", "TurboJobOfferCreate defined")
    local caps = require 'imports/turbo/core/caps'
    H.eq(caps.unavailable().job_offer, nil, "job_offer now available")

    local status, text, o0, o1 = bridge.game_call(bridge.CALL_OP_JOB_OFFER, { 0x1234, 7 }, "test")
    H.eq(status, "ok", text); H.has(text, "20270115"); H.eq(o0, 20270115); H.eq(o1, 42000)
    H.eq(#dll.calls, 1); H.eq(dll.calls[1].op, 1); H.eq(dll.calls[1].jmm, 0x1234); H.eq(dll.calls[1].team, 7)
    H.eq(dll.calls[1].seq, sim:r32(mb + RSEQ), "result tagged with the request number")

    -- the feature end to end: the career's JobMarketManager (type 53) is passed to the DLL
    local jmm = sim:add_manager(53, 0xC90)
    local ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "job offer created"); H.has(msg, "Everton"); H.has(msg, "42000")
    H.eq(#dll.calls, 2); H.eq(dll.calls[2].jmm, jmm, "JobMarketManager address passed"); H.eq(dll.calls[2].team, 7)

    -- failed in the DLL: the text comes back as the reason
    dll.answer = { status = -1, text = "this club already made you an offer on 20270110: check the Job Offers screen" }
    ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, false); H.has(msg, "already made you an offer")

    -- queued (called off the game thread): the module reports it, the next event picks the outcome up and publishes it
    dll.answer = { status = 2, text = "queued for the game thread: it runs on the next career-mode event (advance a day)" }
    ok, msg = run({ enabled = true, teamid = 7, confirm = true })
    H.eq(ok, true, msg); H.has(msg, "queued")
    H.ok(TURBO_STATE.bridge.call_pending ~= nil, "pending call remembered")
    status, text = bridge.game_call(bridge.CALL_OP_JOB_OFFER, { 0x1234, 7 }, "second")
    H.eq(status, "failed"); H.has(text, "still queued")
    H.eq(bridge.check_game_call(), false, "still queued: nothing to report")
    sim:wstr(mb + TEXT, "job offer sent on 20270116 (weekly wage 42000)")
    sim:w32(mb + STATUS, 1)   -- the dispatcher ran it on the game thread
    H.eq(bridge.check_game_call(), true, "outcome picked up")
    H.eq(TURBO_STATE.bridge.call_pending, nil)
    local gc = TURBO_STATE.bridge.game_call
    H.ok(gc and gc.ok == true and gc.text:find("20270116", 1, true) ~= nil, "outcome published for the GUI")
    H.ok(bridge.collect_state().game_call == gc, "bridge_state carries game_call")

    -- an answer for another request number is not trusted
    dll.answer = { status = 1, text = "stale", rseq = 1 }
    status, text = bridge.game_call(bridge.CALL_OP_JOB_OFFER, { 0x1234, 7 }, "x")
    H.eq(status, "failed"); H.has(text, "did not answer")

    -- TurboStandingsRefresh (op 2): the career's StandingsViewManager (type 108) and the manager table, found through
    -- the checked walk of core/mem.lua, plus the comm service and the FCE interface go to the DLL; bridge_state.json
    -- carries the same two addresses for the overlay
    H.eq(type(_G.TurboStandingsRefresh), "function", "TurboStandingsRefresh defined")
    local svm = sim:add_manager(108, 0x500)
    local st = bridge.collect_state()
    H.eq(st.svm, string.format("0x%X", svm), "bridge_state carries the StandingsViewManager")
    H.eq(st.managers, string.format("0x%X", sim.mode_managers), "bridge_state carries the manager table")
    dll.answer = { status = 1, text = "the game's standings view re-read 2 competitions (comp ids 1200, 1300)", out0 = 2, out1 = 2 }
    local okr, rtext, rstatus = _G.TurboStandingsRefresh()
    H.eq(okr, true, tostring(rtext)); H.eq(rstatus, "ok"); H.has(rtext, "re-read 2")
    local last = dll.calls[#dll.calls]
    H.eq(last.op, 2, "op standings_refresh")
    H.eq(last.jmm, svm, "svm passed"); H.eq(last.team, sim.mode_managers, "manager table passed")
    H.eq(last.a3, sim.plugins[0x1297f047], "comm service passed"); H.eq(last.a4, sim.plugins[0x0a613b9a] or 0, "ifce passed")
    dll.answer = { status = 2, text = "queued for the game thread: it runs at the next game tick" }
    okr, rtext, rstatus = _G.TurboStandingsRefresh()
    H.eq(okr, true); H.eq(rstatus, "queued")
    TURBO_STATE.bridge.call_pending = nil
    sim.in_cm = false
    st = bridge.collect_state()
    H.eq(st.svm, "0x0", "no StandingsViewManager outside a career")
    sim.in_cm = true

    -- an older Turbo.dll (mailbox version 1) has no call block
    sim:w32(mb + 4, 1)
    status, text = bridge.game_call(bridge.CALL_OP_JOB_OFFER, { 0x1234, 7 }, "x")
    H.eq(status, "unavailable"); H.has(text, "older")
    sim:w32(mb + 4, 2)
    package.loadlib = real_loadlib
    _G.TurboJobOfferCreate = nil
    _G.TurboStandingsRefresh = nil
end)

-- 1.1.3 hotfix: on the game thread the job offer's game call runs the game's MakeOffer at once, and the game posts
-- career-mode events before it returns; Live Editor runs Turbo's dispatcher for them on the spot. 1.0.2-1.1.2 picked the
-- same, not yet acknowledged mailbox command up again from that nested event and ran it again, nesting until the game
-- died. The command must run exactly once and nothing may nest.
H.case("re-entrant career events during the job offer's game call: the command runs once, nothing nests", function()
    local bridge = require 'imports/turbo/bridge'
    local events = require 'imports/turbo/core/events'
    local json = require 'imports/external/json'
    local S = TURBO_STATE.bridge
    S.next_dll_check = 0
    local mb = bridge.mailbox_address()
    H.ok(mb ~= nil, "mailbox found")
    sim:w32(mb + 4, 2)
    S.call_pending = nil
    -- the running bridge: its tap on Live Editor's career-mode events (bridge.start)
    S.autoload_pending = false
    events.set_tap("bridge", bridge.on_career_event)
    H.eq(events.ensure_registered(), true, "dispatcher registered")
    local CSEQ, CSTATUS, CRSEQ, CTEXT = 0x2028, 0x2024, 0x202C, 0x2060
    local calls, depth, max_depth = 0, 0, 0
    S.native_game_call = function()   -- Turbo.dll's turbo_game_call, on the game thread: runs the call at once
        calls = calls + 1
        depth = depth + 1
        if depth > max_depth then max_depth = depth end
        if calls < 20 then   -- (bounded, so the unfixed code fails here instead of overflowing the test's stack)
            sim:fire("post__CareerModeEvent", 0, 7, 0)
            sim:fire("post__CareerModeEvent", 0, events.SYNTHETIC_ID, 0)
        end
        sim:wstr(mb + CTEXT, "job offer sent on 20270115 (weekly wage 42000)")
        sim:w32(mb + CRSEQ, sim:r32(mb + CSEQ))
        sim:w32(mb + CSTATUS, 1)
        depth = depth - 1
    end
    S.natives_installed = nil
    H.eq(bridge.install_natives(), true, "TurboJobOfferCreate on top of the game call")
    local runs = 0
    local turbo = H.turbo()
    local real_run = turbo.run
    turbo.run = function(...) runs = runs + 1; return real_run(...) end
    -- the Turbo window's Create job offer: a run command in the mailbox, then the synthetic event
    local cmd = json.encode({ op = "run", module = "job_offer", overrides = { enabled = true, teamid = 7, confirm = true } })
    for i = 0, 0xFFF do sim:wb(mb + 0x20 + i, 0) end
    for i = 1, #cmd do sim:wb(mb + 0x20 + i - 1, cmd:byte(i)) end
    local seq = sim:r32(mb + 0xC) + 1
    sim:w32(mb + 8, seq)
    sim:fire("post__CareerModeEvent", 0, events.SYNTHETIC_ID, 0)
    H.eq(runs, 1, "job_offer ran once")
    H.eq(calls, 1, "the game call ran once")
    H.eq(max_depth, 1, "nothing nested")
    H.eq(sim:r32(mb + 0xC), seq, "command acknowledged")
    H.eq(sim:r32(mb + 0x10), 1, "command succeeded")
    H.has(ReadString(mb + 0x1020, 4096), "job offer created")
    -- the next events do not run it again
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    sim:fire("post__CareerModeEvent", 0, events.SYNTHETIC_ID, 0)
    H.eq(runs, 1); H.eq(calls, 1)
    H.eq(TURBO_STATE.dispatching, false, "dispatcher free again")
    H.eq(turbo.running, nil, "no action marked running")
    turbo.run = real_run
    S.native_game_call = nil
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
