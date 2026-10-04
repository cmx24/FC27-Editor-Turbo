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
        dll.calls[#dll.calls + 1] = { op = sim:r32(mb + OP), seq = seq, jmm = sim:r64(mb + ARGS), team = sim:r64(mb + ARGS + 8) }
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

    -- an older Turbo.dll (mailbox version 1) has no call block
    sim:w32(mb + 4, 1)
    status, text = bridge.game_call(bridge.CALL_OP_JOB_OFFER, { 0x1234, 7 }, "x")
    H.eq(status, "unavailable"); H.has(text, "older")
    sim:w32(mb + 4, 2)
    package.loadlib = real_loadlib
    _G.TurboJobOfferCreate = nil
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
