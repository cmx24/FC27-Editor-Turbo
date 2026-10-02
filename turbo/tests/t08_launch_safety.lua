-- luacheck: globals package
package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t08 launch safety: while the game launches Turbo calls no game native, loads only Turbo.dll in launch mode, reads no stale mailbox")

-- Every native that reaches into the game (everything the simulator provides except logging, message boxes and
-- the event-handler registry, which Live Editor's own scripts use at any time)
local NATIVES = {
    "ReadBytes", "ReadShort", "ReadInteger", "ReadQword", "ReadFloat", "ReadString",
    "WriteBytes", "WriteShort", "WriteInteger", "WriteQword", "WriteFloat", "WriteString",
    "AOBScan", "AllocateMemory", "DeallocateMemory", "WriteJMP",
    "IsInCM", "GetPlugin", "GetDBMeta", "GetSaveUID", "GetCurrentDate", "GetPlayerName", "GetTeamName",
    "GetTeamIdFromPlayerId", "GetCompetitionNameByObjID", "GetCompetitionNameByID", "SetPlayerForm",
    "SetPlayerMorale", "SetPlayerFitness", "GetPlayersStats", "PlayerExists", "DeletePlayer", "TerminateLoan",
    "cTransferPlayer", "cLoanPlayer", "cReleasePlayer", "cIsPlayerTransferListed", "cIsPlayerLoanListed",
    "cAddPlayerToTransferList", "cAddPlayerToLoanList", "cRemovePlayerFromLists",
    "cRemovePlayerFromTransferList", "cRemovePlayerFromLoanList", "cGetTransferBans", "cAddTransferBan",
    "cRemoveTransferBan", "cSaveTransferBans", "PlayerDevelopmentManagerLoad", "PlayerDevelopmentManagerSave",
    "PlayerDevelopmentManagerAddPlayer", "PlayerDevelopmentManagerRemovePlayer", "GetDBTableRows",
}

local calls, mem_addrs, loadlib_calls, loadlib_paths = {}, {}, 0, {}
local real_loadlib = package.loadlib
package.loadlib = function(path)
    loadlib_calls = loadlib_calls + 1
    loadlib_paths[#loadlib_paths + 1] = path
    return true
end

local function instrument()
    calls, mem_addrs, loadlib_calls, loadlib_paths = {}, {}, 0, {}
    for _, name in ipairs(NATIVES) do
        local orig = _G[name]
        if type(orig) == "function" then
            _G[name] = function(a, ...)
                calls[name] = (calls[name] or 0) + 1
                if name:match("^Read") or name:match("^Write") then mem_addrs[#mem_addrs + 1] = a end
                return orig(a, ...)
            end
        end
    end
end

local function native_calls()
    local n, names = 0, {}
    for k, c in pairs(calls) do n = n + c; names[#names + 1] = k .. "x" .. c end
    table.sort(names)
    return n, table.concat(names, ", ")
end

local function handler_events(sim)
    local out = {}
    for ev, list in pairs(sim.handlers) do if #list > 0 then out[#out + 1] = ev end end
    table.sort(out)
    return out
end

local function install_dll_stub()
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
end

local function launch_mode()
    local text = H.read(H.out("turbo_gui_load.json"))
    return text and (require 'imports/external/json').decode(text).mode or nil
end

H.case("default launch: no game native; Turbo.dll loaded once in launch mode; only the documented career event", function()
    local sim = H.setup({ in_cm = false })
    install_dll_stub()
    instrument()
    H.turbo().boot({ at_launch = true })
    local n, names = native_calls()
    H.eq(n, 0, "no game native called (" .. names .. ")")
    H.eq(loadlib_calls, 1, "package.loadlib called once")
    H.has(loadlib_paths[1] or "", "/turbo/Turbo.dll", "only Turbo.dll")
    H.eq(launch_mode(), "launch", "launch mode: the DLL touches nothing until Live Editor reports Initial setup done")
    local evs = handler_events(sim)
    H.eq(#evs, 1, "one event")
    H.eq(evs[1], "post__CareerModeEvent", "only the documented career event")
    H.eq(H.read(H.out("bridge_state.json")), nil, "no bridge file written")
    H.eq(H.read(H.out("bridge_meta.json")), nil, "no meta file written")
end)

H.case("a crash flag from the last start is reported in Live Editor's log at launch (pure file I/O)", function()
    local function logs_text(sim)
        local t = {}
        for _, l in ipairs(sim.logs) do t[#t + 1] = l.text end
        return table.concat(t, "\n")
    end
    local sim = H.setup({ in_cm = false })
    install_dll_stub()
    local f = assert(io.open(H.out("turbo_gui_start.flag"), "wb"))
    f:write("hooks being installed / first frames (process 1, tick 2)\n")
    f:close()
    instrument()
    H.turbo().boot({ at_launch = true })
    local n, names = native_calls()
    H.eq(n, 0, "still no game native (" .. names .. ")")
    H.has(logs_text(sim), "last start did not finish (hooks being installed / first frames (process 1, tick 2)): trying once more")
    H.eq(loadlib_calls, 1, "Turbo.dll still loaded: it decides about the retry")
    local sim2 = H.setup({ in_cm = false })
    install_dll_stub()
    f = assert(io.open(H.out("turbo_gui_start.flag"), "wb"))
    f:write("RETRY: hooks being installed (process 3, tick 4)\n")
    f:close()
    H.turbo().boot({ at_launch = true })
    H.has(logs_text(sim2), "Turbo GUI stays off: its last two starts did not finish")
    H.has(logs_text(sim2), "Delete turbo_output\\turbo_gui_start.flag")
end)

H.case("gui.autoload=false: launch loads nothing and registers no event handler", function()
    local sim = H.setup({ in_cm = false, config = { gui = { autoload = false } } })
    install_dll_stub()
    instrument()
    H.turbo().boot({ at_launch = true })
    local n, names = native_calls()
    H.eq(n, 0, "no game native called (" .. names .. ")")
    H.eq(loadlib_calls, 0, "package.loadlib not called")
    H.eq(#handler_events(sim), 0, "no event handler registered")
    H.eq(launch_mode(), nil, "no mode file")
end)

H.case("lua\\autorun\\turbo_boot.lua at launch: same as boot, and every step is in turbo_boot.log", function()
    local sim = H.setup({ in_cm = false })
    install_dll_stub()
    instrument()
    dofile(H.LE .. "/lua/autorun/turbo_boot.lua")
    local n, names = native_calls()
    H.eq(n, 0, "no game native called (" .. names .. ")")
    H.eq(loadlib_calls, 1, "Turbo.dll loaded once")
    H.eq(launch_mode(), "launch", "in launch mode")
    H.eq(#handler_events(sim), 1, "one event handler")
    local trace = H.read(H.out("turbo_boot.log"))
    H.ok(trace, "turbo_boot.log written")
    H.has(trace, "autorun: turbo_boot.lua started")
    H.has(trace, "boot: start (game launch)")
    H.has(trace, "boot: loading turbo\\Turbo.dll")
    H.has(trace, "boot: load_gui returned true")
    H.has(trace, "boot: done")
    H.has(trace, "autorun: turbo_boot.lua finished")
end)

H.case("Turbo.dll missing or package.loadlib missing at launch: boot still finishes and says why", function()
    H.setup({ in_cm = false })
    instrument()
    H.turbo().boot({ at_launch = true })
    H.eq(loadlib_calls, 0, "no DLL file: nothing loaded")
    H.has(H.read(H.out("turbo_boot.log")), "Turbo.dll not found")
    install_dll_stub()
    TURBO_STATE.bridge.gui_loaded = false
    local saved = package.loadlib
    package.loadlib = nil
    local ok = pcall(H.turbo().boot, { at_launch = true })
    package.loadlib = saved
    H.eq(ok, true, "boot does not fail")
    H.has(H.read(H.out("turbo_boot.log")), "TurboInjector.exe")
    H.has(H.read(H.out("turbo_boot.log")), "boot: done")
end)

H.case("auto features enabled: launch registers only the documented career event and still calls no native", function()
    local sim = H.setup({ in_cm = false, config = { auto = { form_morale = { enabled = true }, pap_playstyles = { enabled = true } } } })
    instrument()
    local enabled = H.turbo().boot({ at_launch = true })
    local n, names = native_calls()
    H.eq(n, 0, "no game native called, not even IsInCM (" .. names .. ")")
    H.eq(#enabled, 2, "both features configured")
    local evs = handler_events(sim)
    H.eq(#evs, 1, "one event")
    H.eq(evs[1], "post__CareerModeEvent", "only the documented career event")
    H.eq(loadlib_calls, 0, "no DLL")
end)

H.case("gui.autoload=true: the bridge reads the game only from the first career event on; Turbo.dll loaded once", function()
    local sim = H.setup({ in_cm = false, config = { gui = { autoload = true } } })
    W.build(sim, {})
    install_dll_stub()
    instrument()
    H.turbo().boot({ at_launch = true })
    local n, names = native_calls()
    H.eq(n, 0, "launch: no game native called (" .. names .. ")")
    H.eq(loadlib_calls, 1, "launch: Turbo.dll in launch mode")
    sim.in_cm = true
    sim:fire("post__CareerModeEvent", 0, 15, 0)
    H.eq(loadlib_calls, 1, "the first career event does not load it again")
    H.ok(native_calls() > 0, "and only now reads the game")
    H.ok(H.read(H.out("bridge_state.json")), "bridge state written on the first career event")
    sim:fire("post__CareerModeEvent", 0, 15, 0)
    H.eq(loadlib_calls, 1, "loaded once")
    local evs = handler_events(sim)
    H.eq(#evs, 1, "one event")
    H.eq(evs[1], "post__CareerModeEvent", "never an undocumented init event")
end)

H.case("a stale bridge_dll.json is never trusted: no memory is touched on its word", function()
    local sim = H.setup({ in_cm = true })
    W.build(sim, {})
    install_dll_stub()
    local json = require 'imports/external/json'
    local STALE = 0x7FF000001000
    local function write_dll(updated)
        local t = { mailbox = string.format("0x%X", STALE), session = "OLD", gui_version = "0.2.4" }
        if updated ~= nil then t.updated = updated end
        local f = assert(io.open(H.out("bridge_dll.json"), "wb"))
        f:write(json.encode(t))
        f:close()
    end
    local bridge = require 'imports/turbo/bridge'
    local function touched()
        for _, a in ipairs(mem_addrs) do if a == STALE or a == STALE + 0x14 then return true end end
        return false
    end
    for label, stamp in pairs({ ["an hour old"] = os.time() - 3600, ["from the future"] = os.time() + 3600, ["missing"] = false }) do
        instrument()
        TURBO_STATE.bridge.mailbox = nil
        write_dll(stamp ~= false and stamp or nil)
        H.turbo().boot()
        bridge.start()
        sim:fire("post__CareerModeEvent", 0, 15, 0)
        bridge.poll_mailbox(true)
        H.eq(touched(), false, "stamp " .. label .. ": stale mailbox address not read")
        H.eq(TURBO_STATE.bridge.mailbox, nil, "stamp " .. label .. ": not connected")
    end
    -- a live stamp from a real mailbox is used
    local MB = sim:alloc(0x2020, 16)
    sim:w32(MB, 0x4F425254)
    local f = assert(io.open(H.out("bridge_dll.json"), "wb"))
    f:write(json.encode({ mailbox = string.format("0x%X", MB), session = "NOW", gui_version = "0.2.4", updated = os.time() }))
    f:close()
    TURBO_STATE.bridge.mailbox = nil
    bridge.poll_mailbox(true)
    H.eq(TURBO_STATE.bridge.mailbox, MB, "fresh file connects")
end)

H.case("the breadcrumb log stays bounded", function()
    H.setup({ in_cm = false })
    local trace = require 'imports/turbo/core/trace'
    local path = trace.path()
    local f = assert(io.open(path, "wb"))
    f:write(string.rep("x", 70 * 1024))
    f:close()
    trace.step("after the limit")
    local text = H.read(path)
    H.ok(#text < 1024, "log restarted instead of growing without limit")
    H.has(text, "after the limit")
end)

package.loadlib = real_loadlib
H.finish()
