-- luacheck: globals package ReadString
package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t13 game thread: Turbo.dll's synthetic career event runs the GUI mailbox at once, nothing else")

local MAGIC = 0x4F425254
local DAY_PASSED = 15
local SYNTHETIC = 0x7E7E0001  -- turbogui/src/core/gamethread.h kSyntheticCareerEvent

local sim = H.setup({ in_cm = true, no_gui = true })
W.build(sim, {})

local json = require 'imports/external/json'

local function read_json(name)
    local d = H.read(H.out(name))
    if not d then return nil end
    return json.decode(d)
end

local function write_out(name, tbl)
    local f = assert(io.open(H.out(name), "wb"))
    f:write(type(tbl) == "string" and tbl or json.encode(tbl))
    f:close()
end

-- Mailbox laid out exactly like Turbo.dll's (src/core/bridge.h)
local MB = sim:alloc(0x2020, 16)
sim:w32(MB, MAGIC)
sim:w32(MB + 4, 1)
local seq = 0
local function submit(text)
    for i = 0, 0xFFF do sim:wb(MB + 0x1020 + i, 0) end
    for i = 0, 0xFFF do sim:wb(MB + 0x20 + i, 0) end
    for i = 1, #text do sim:wb(MB + 0x20 + i - 1, text:byte(i)) end
    seq = seq + 1
    sim:w32(MB + 8, seq)
end
local function result()
    return sim:r32(MB + 0xC), sim:r32(MB + 0x10), ReadString(MB + 0x1020, 4096)
end

-- package.loadlib stub: "*" (loading Turbo.dll) succeeds, "turbo_game_pump" hands back a counting stub
local pumps = 0
local real_loadlib = package.loadlib
package.loadlib = function(path, sym)
    if sym == "turbo_game_pump" then return function() pumps = pumps + 1; return 0 end end
    return true
end

local events = require 'imports/turbo/core/events'
local bridge = require 'imports/turbo/bridge'

local function dll_json()
    write_out("bridge_dll.json", { mailbox = string.format("0x%X", MB), session = "T", gui_version = "0.4.0", updated = os.time() })
end

H.case("the synthetic event id is the one Turbo.dll sends and no career-mode enum uses it", function()
    H.eq(events.SYNTHETIC_ID, SYNTHETIC, "id matches core/gamethread.h")
    H.eq(events.is_synthetic(SYNTHETIC), true)
    H.eq(events.is_synthetic(DAY_PASSED), false)
    H.eq(events.is_synthetic(nil), false)
    for k, v in pairs(_G) do
        if type(k) == "string" and (k:match("^ENUM_FCEGameModesCM_EVENT_MSG_") or k:match("^ENUM_CM_EVENT_MSG_")) then
            H.ok(v ~= SYNTHETIC, "enum " .. k .. " does not collide")
        end
    end
end)

H.case("before the bridge runs, the synthetic event is ignored (nothing can be waiting)", function()
    H.turbo().boot()
    bridge.arm({})
    H.eq(events.ensure_registered(), true, "dispatcher registered")
    H.eq(TURBO_STATE.bridge.autoload_pending, true, "armed: waits for the first real event")
    submit('{"op":"ping"}')
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    H.ok(sim:r32(MB + 0xC) ~= seq, "not acknowledged")
    H.eq(TURBO_STATE.bridge.autoload_pending, true, "the bridge did not start on a synthetic event")
    TURBO_STATE.bridge.autoload_pending = false
end)

H.case("a queued command is answered by the synthetic event, without touching the state files", function()
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
    local ok, msg = bridge.start()
    H.eq(ok, true, msg)
    dll_json()
    sim:fire("post__CareerModeEvent", 0, 7, 0)  -- a real event connects the mailbox
    local st0 = read_json("bridge_state.json")
    H.ok(st0, "state written by the real event")
    local pumps0 = pumps
    submit('{"op":"ping"}')
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    local ack, status, text = result()
    H.eq(ack, seq, "acknowledged at once")
    H.eq(status, 1, "ok")
    H.eq(text, "pong", "result")
    H.eq(pumps, pumps0 + 1, "the native pump ran during the synthetic event (Turbo.dll counts it as proof)")
    H.eq(read_json("bridge_state.json").seq, st0.seq, "state file not rewritten")
    local hb = sim:r32(MB + 0x14)
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    H.ok(sim:r32(MB + 0x14) ~= hb, "heartbeat moves on a synthetic event too")
    H.eq(sim:r32(MB + 0xC), seq, "no command: ack unchanged")
end)

H.case("a module command runs from the synthetic event exactly like from a real one", function()
    local before = sim:count_calls("SetPlayerForm")
    submit(json.encode({ op = "run", module = "form_morale", overrides = { form = 70, morale = 60, fitness = 0 } }))
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    local ack, status, text = result()
    H.eq(ack, seq); H.eq(status, 1, "ok: " .. text)
    H.eq(sim:count_calls("SetPlayerForm") - before, 26, "whole squad")
end)

H.case("id-keyed listeners never see the synthetic event; taps do", function()
    local hits, taps = 0, 0
    events.set_listener("t09", { [DAY_PASSED] = true }, function() hits = hits + 1 end)
    events.set_tap("t09", function(id) if id == SYNTHETIC then taps = taps + 1 end end)
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    H.eq(hits, 0, "listener untouched")
    H.eq(taps, 1, "tap ran")
    sim:fire("post__CareerModeEvent", 0, DAY_PASSED, 0)
    H.eq(hits, 1, "real event still dispatched")
    events.clear_listener("t09")
    events.clear_tap("t09")
end)

H.case("a synthetic event with a broken mailbox is harmless", function()
    sim:w32(MB, 0)
    submit('{"op":"ping"}')
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)
    H.ok(sim:r32(MB + 0xC) ~= seq, "not acknowledged")
    sim:w32(MB, MAGIC)
    dll_json()
    TURBO_STATE.bridge.next_dll_check = 0
    sim:fire("post__CareerModeEvent", 0, SYNTHETIC, 0)  -- the synthetic poll is forced: reconnects at once
    H.eq(sim:r32(MB + 0xC), seq, "acknowledged after reconnect")
end)

package.loadlib = real_loadlib
H.finish()
