package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t12 game hooks: the Lua side pumps Turbo.dll's game-thread dispatcher on career-mode events")

-- The GUI's mailbox is live (bridge_dll.json fresh, magic in memory): the bridge looks for turbo_game_pump in Turbo.dll
local sim = H.setup({ in_cm = true, no_gui = true })  -- this file drives its own mailbox (like t07)
W.build(sim, {})

local MAGIC = 0x4F425254
local MB = sim:alloc(0x2020, 16)
sim:w32(MB, MAGIC)
sim:w32(MB + 4, 1)
local json = require 'imports/external/json'

local function write_out(name, t)
    local f = assert(io.open(H.out(name), "wb"))
    f:write(json.encode(t))
    f:close()
end

-- package.loadlib stub: records calls; "turbo_game_pump" returns whatever the test wants
local loadlib_calls = {}
local pump_result = nil
local pumps = 0
package.loadlib = function(path, sym)
    loadlib_calls[#loadlib_calls + 1] = { path = path, sym = sym }
    if sym == "turbo_game_pump" then return pump_result end
    return true
end

local bridge = require 'imports/turbo/bridge'
os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))

local function events(n)
    for _ = 1, n do sim:fire("post__CareerModeEvent", 0, 7, 0) end
end

H.case("without the GUI mailbox nothing is looked up", function()
    local ok = bridge.start()
    H.eq(ok, true)
    TURBO_STATE.bridge.mailbox = nil
    os.remove(H.out("bridge_dll.json"))
    local n = #loadlib_calls
    events(3)
    H.eq(#loadlib_calls, n, "no loadlib without a live mailbox")
    H.eq(bridge.pump_native(), false, "pump_native returns false")
end)

H.case("a Turbo.dll without turbo_game_pump (older build): one lookup, retried every 60 events, never an error", function()
    write_out("bridge_dll.json", { mailbox = string.format("0x%X", MB), session = "T", gui_version = "1.1.3", updated = os.time() })
    pump_result = nil
    TURBO_STATE.bridge.native_pump, TURBO_STATE.bridge.native_pump_tries = nil, nil
    local n = #loadlib_calls
    events(1)
    H.eq(TURBO_STATE.bridge.mailbox, MB, "mailbox connected")
    H.eq(#loadlib_calls, n + 1, "looked up once")
    H.eq(loadlib_calls[#loadlib_calls].sym, "turbo_game_pump", "the pump symbol")
    H.has(loadlib_calls[#loadlib_calls].path, "Turbo.dll")
    events(59)
    H.eq(#loadlib_calls, n + 1, "not retried on every event")
    events(1)
    H.eq(#loadlib_calls, n + 2, "retried after 60 more events")
    H.eq(TURBO_STATE.bridge.native_pump, false, "remembered as missing")
end)

H.case("turbo_game_pump found: called on every career-mode event, on the event thread", function()
    pump_result = function() pumps = pumps + 1; return 0 end
    TURBO_STATE.bridge.native_pump, TURBO_STATE.bridge.native_pump_tries = nil, nil
    local n = #loadlib_calls
    events(1)
    H.eq(#loadlib_calls, n + 1, "looked up once")
    H.eq(pumps, 1, "pumped on the first event")
    events(5)
    H.eq(pumps, 6, "pumped on every event")
    H.eq(#loadlib_calls, n + 1, "the function is kept")
    H.eq(TURBO_STATE.bridge.pumps, 6, "counted")
    H.eq(bridge.pump_native(), true, "pump_native returns true")
    H.eq(pumps, 7)
end)

H.case("a pump that throws never breaks the event handler", function()
    TURBO_STATE.bridge.native_pump = function() error("boom") end
    local okh = pcall(events, 2)
    H.eq(okh, true, "event handler survives")
    TURBO_STATE.bridge.native_pump = pump_result
    events(1)
    H.eq(pumps, 8, "pumping resumes")
end)

H.case("no unmapped memory reads", function()
    H.eq(sim.unmapped_reads, 0)
end)

H.finish()
