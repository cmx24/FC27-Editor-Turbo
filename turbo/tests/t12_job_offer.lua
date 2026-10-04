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

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0, "unmapped reads")
end)

H.finish()
