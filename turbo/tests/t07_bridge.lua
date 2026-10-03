-- luacheck: globals package ReadString
package.path = (debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./") .. "?.lua;" .. package.path
local H = require 'h'
local W = require 'world'

print("t07 Turbo GUI bridge: state files, mailbox commands, GUI settings, Turbo.dll loading")

local MAGIC = 0x4F425254
local DAY_PASSED, POST_LOAD_PREPARE = 15, 29

local sim = H.setup({ in_cm = true })
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

-- package.loadlib stub: records calls, never loads anything
local loadlib_calls = {}
local real_loadlib = package.loadlib
package.loadlib = function(path, sym)
    loadlib_calls[#loadlib_calls + 1] = { path = path, sym = sym }
    return true
end

local bridge = require 'imports/turbo/bridge'

-- bridge_dll.json as Turbo.dll writes it: the address plus a live timestamp
local function dll_json(fields)
    local t = { mailbox = string.format("0x%X", MB), session = "T", gui_version = "0.2.4", updated = os.time() }
    for k, v in pairs(fields or {}) do t[k] = v end
    write_out("bridge_dll.json", t)
end

H.case("bridge.start writes bridge_meta.json and bridge_state.json the GUI can read, and loads Turbo.dll", function()
    H.turbo().boot()
    H.eq(#loadlib_calls, 0, "boot alone loads nothing")
    H.eq(read_json("bridge_state.json"), nil, "boot alone writes nothing")
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
    local ok, msg = bridge.start()
    H.eq(ok, true, msg)
    H.eq(#loadlib_calls, 1, "start loads Turbo.dll once")
    H.eq(sim.handlers["post__LEInitDoneEvent"], nil, "no undocumented init event registered")
    local meta = read_json("bridge_meta.json")
    H.ok(meta, "meta written")
    H.eq(meta.shortname_name_tables_map.plyr, "players", "players table name")
    H.eq(meta.field_desc_map.plyr.pid_.name, "playerid", "field name")
    H.eq(meta.field_desc_map.plyr.pid_.depth, 21, "field depth")
    H.eq(meta.field_desc_map.cmts.p001.min, -1, "field min")
    local st = read_json("bridge_state.json")
    H.ok(st, "state written")
    H.eq(st.db_service, string.format("0x%X", sim.plugins[0x0ae932d0] - 8), "db service address")
    H.eq(st.comm_service, string.format("0x%X", sim.plugins[0x1297f047]), "comm service address")
    H.eq(st.in_cm, true, "in career")
    H.eq(st.user_team, W.USER_TEAM, "user team")
    H.eq(st.date.year, 2027); H.eq(st.date.month, 1); H.eq(st.date.day, 15)
    H.eq(st.le_version, "v27.1.0", "LE version")
    H.ok(st.db_gen >= 1, "db_gen set")
    H.eq(st.settings.dry_run, false, "settings: dry run")
    H.eq(st.settings.auto.form_morale.enabled, false, "settings: auto form")
end)

H.case("a new day updates the state without a new database generation; a save load bumps it", function()
    local st0 = read_json("bridge_state.json")
    sim.date = { day = 16, month = 1, year = 2027 }
    sim:fire("post__CareerModeEvent", 0, DAY_PASSED, 0)
    local st1 = read_json("bridge_state.json")
    H.eq(st1.date.day, 16, "date updated")
    H.ok(st1.seq > st0.seq, "seq increased")
    H.eq(st1.db_gen, st0.db_gen, "same db_gen")
    sim:fire("post__CareerModeEvent", 0, 7, 0)  -- nothing changed: no rewrite
    H.eq(read_json("bridge_state.json").seq, st1.seq, "unchanged state not rewritten")
    sim:fire("post__CareerModeEvent", 0, POST_LOAD_PREPARE, 0)
    H.eq(read_json("bridge_state.json").db_gen, st1.db_gen + 1, "db_gen bumped on load")
end)

H.case("leaving career mode is a structural change", function()
    local st0 = read_json("bridge_state.json")
    sim.in_cm = false
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local st1 = read_json("bridge_state.json")
    H.eq(st1.in_cm, false, "left career")
    H.eq(st1.user_team, 0, "no user team outside career")
    H.eq(st1.date, nil, "no date outside career")
    H.eq(st1.db_gen, st0.db_gen + 1, "db_gen bumped")
    sim.in_cm = true
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local st2 = read_json("bridge_state.json")
    H.eq(st2.in_cm, true); H.eq(st2.db_gen, st0.db_gen + 2, "bumped again")
end)

H.case("mailbox: ping is answered on the next career event, with a heartbeat", function()
    dll_json()
    TURBO_STATE.bridge.next_dll_check = 0  -- the bridge looks for bridge_dll.json at most once a second
    submit('{"op":"ping"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local ack, status, text = result()
    H.eq(ack, seq, "acknowledged")
    H.eq(status, 1, "ok")
    H.eq(text, "pong", "result")
    H.ok(sim:r32(MB + 0x14) > 0, "heartbeat written")
    local hb = sim:r32(MB + 0x14)
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    H.ok(sim:r32(MB + 0x14) ~= hb, "heartbeat moves on every event")
    H.eq(sim:r32(MB + 0xC), seq, "no command, ack unchanged")
end)

H.case("mailbox: run executes a module with GUI overrides and shows no message box", function()
    local boxes = #sim.boxes
    local before = sim:count_calls("SetPlayerForm")
    submit(json.encode({ op = "run", module = "form_morale", overrides = { form = 77, morale = 66, fitness = 0 } }))
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local ack, status, text = result()
    H.eq(ack, seq); H.eq(status, 1, "ok: " .. text)
    H.eq(sim:count_calls("SetPlayerForm") - before, 26, "whole squad")
    H.eq(sim.calls.SetPlayerForm[#sim.calls.SetPlayerForm][2], 77, "form from GUI")
    H.eq(sim.calls.SetPlayerMorale[#sim.calls.SetPlayerMorale][2], 66, "morale from GUI")
    H.eq(#sim.boxes, boxes, "silent")
end)

H.case("mailbox: failures come back as status 0 with the reason", function()
    submit('{"op":"run","module":"sharpness"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local _, status, text = result()
    H.eq(status, 0); H.has(text, "unknown Turbo module")
    submit('{"op":"run",')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    _, status, text = result()
    H.eq(status, 0); H.has(text, "not valid JSON")
    submit('{"op":"explode"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    _, status, text = result()
    H.eq(status, 0); H.has(text, "unknown op")
    H.eq(sim:r32(MB + 0xC), seq, "every command acknowledged")
end)

H.case("GUI settings: auto form/morale switched on in the GUI, then boot through the mailbox", function()
    write_out("gui_settings.json", { gui = { toggle_key = 0x77 },
        auto = { form_morale = { enabled = true, form = 88, morale = 55, fitness = 0 } } })
    local before = sim:count_calls("SetPlayerForm")
    submit('{"op":"boot"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local _, status, text = result()
    H.eq(status, 1); H.has(text, "form_morale")
    H.eq(sim:count_calls("SetPlayerForm") - before, 26, "applied at boot")
    sim:fire("post__CareerModeEvent", 0, DAY_PASSED, 0)
    H.eq(sim:count_calls("SetPlayerForm") - before, 52, "applied on day passed")
    H.eq(sim.calls.SetPlayerForm[#sim.calls.SetPlayerForm][2], 88, "GUI form value")
    local st = read_json("bridge_state.json")
    H.eq(st.settings.auto.form_morale.enabled, true, "state reports the effective setting")
    H.eq(st.settings.auto.form_morale.form, 88, "effective form")
    H.eq(#sim.handlers["post__CareerModeEvent"], 1, "still one dispatcher")
end)

H.case("GUI settings: dry run from the GUI wins over turbo_config.json; bad file is ignored", function()
    write_out("gui_settings.json", { turbo = { dry_run = true } })
    local cfg = require('imports/turbo/core/config').load()
    H.eq(cfg.turbo.dry_run, true, "dry run on")
    H.eq(cfg.auto.form_morale.enabled, false, "auto back to turbo_config.json")
    H.eq(cfg.gui.autoload, true, "the GUI loads by itself by default (launch mode)")
    write_out("gui_settings.json", "{ not json")
    local cfg2, info = require('imports/turbo/core/config').load()
    H.eq(cfg2.turbo.dry_run, false, "ignored")
    H.has(info.gui_error, "gui_settings.json ignored")
    write_out("gui_settings.json", "{}")
    submit('{"op":"boot"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local n = 0
    for _ in pairs(TURBO_STATE.listeners) do n = n + 1 end
    H.eq(n, 0, "auto switched off again")
end)

H.case("mailbox: refresh rewrites meta and bumps the database generation", function()
    local g = read_json("bridge_state.json").db_gen
    os.remove(H.out("bridge_meta.json"))
    submit('{"op":"refresh"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    local _, status = result()
    H.eq(status, 1)
    H.ok(read_json("bridge_meta.json"), "meta rewritten")
    H.eq(read_json("bridge_state.json").db_gen, g + 1, "db_gen bumped")
end)

H.case("mailbox with a wrong magic is not used", function()
    sim:w32(MB, 0)
    submit('{"op":"ping"}')
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    H.ok(sim:r32(MB + 0xC) ~= seq, "not acknowledged")
    sim:w32(MB, MAGIC)
    sim:fire("post__CareerModeEvent", 0, 7, 0)  -- reconnect after at most one second
    dll_json()
    H.ok(bridge.poll_mailbox(true) or sim:r32(MB + 0xC) == seq, "forced reconnect")
    H.eq(sim:r32(MB + 0xC), seq, "acknowledged after reconnect")
end)

H.case("outside career mode: turbo_exec.lua runs the queued command", function()
    sim.in_cm = false
    submit(json.encode({ op = "run", module = "export_table", overrides = { tables = { "teams" } } }))
    local boxes = #sim.boxes
    H.script("turbo_exec")
    local ack, status, text = result()
    H.eq(ack, seq, "acknowledged")
    H.eq(status, 1, "ok: " .. text)
    H.ok(#sim.boxes > boxes, "script reports")
    H.has(sim.boxes[#sim.boxes].text, "Ran the queued")
    H.script("turbo_exec")
    H.has(sim.boxes[#sim.boxes].text, "No queued command")
    sim.in_cm = true
end)

H.case("Turbo.dll loading through package.loadlib", function()
    local bridge = require 'imports/turbo/bridge'
    TURBO_STATE.bridge.gui_loaded = false
    os.execute(string.format("rm -rf '%s/turbo'", H.LE))
    local ok, msg = bridge.load_gui()
    H.eq(ok, false); H.has(msg, "Turbo.dll not found")
    os.execute(string.format("mkdir -p '%s/turbo' && printf 'MZ' > '%s/turbo/Turbo.dll'", H.LE, H.LE))
    local n = #loadlib_calls
    ok, msg = bridge.load_gui()
    H.eq(ok, true, msg)
    H.eq(#loadlib_calls, n + 1, "loadlib called")
    H.has(loadlib_calls[#loadlib_calls].path, "turbo")
    H.has(loadlib_calls[#loadlib_calls].path, "Turbo.dll")
    H.eq(loadlib_calls[#loadlib_calls].sym, "*", "link only")
    ok = bridge.load_gui()
    H.eq(ok, true); H.eq(#loadlib_calls, n + 1, "loaded once per session")

    TURBO_STATE.bridge.gui_loaded = false
    local saved = package.loadlib
    package.loadlib = nil
    ok, msg = bridge.load_gui()
    H.eq(ok, false); H.has(msg, "TurboInjector.exe")
    package.loadlib = function() return nil, "The specified module could not be found." end
    ok, msg = bridge.load_gui()
    H.eq(ok, false); H.has(msg, "package.loadlib failed")
    package.loadlib = saved
end)

H.case("gui.autoload=false: nothing at launch; true (default): Turbo.dll at launch in launch mode, bridge on the first career event", function()
    TURBO_STATE.bridge.gui_loaded = false
    TURBO_STATE.bridge.started = false
    H.write_config({ gui = { autoload = false } })
    local n = #loadlib_calls
    H.turbo().boot({ at_launch = true })
    H.eq(#loadlib_calls, n, "autoload=false: not loaded at launch")
    H.turbo().boot()
    H.eq(#loadlib_calls, n, "autoload=false: not loaded by boot either")
    H.write_config({ gui = { autoload = true } })
    os.remove(H.out("bridge_state.json"))
    os.remove(H.out("turbo_gui_load.json"))
    H.turbo().boot({ at_launch = true })
    H.eq(#loadlib_calls, n + 1, "autoload=true: Turbo.dll loaded at launch")
    H.eq(read_json("turbo_gui_load.json").mode, "launch", "in launch mode: the DLL waits for Live Editor's setup")
    H.eq(read_json("bridge_state.json"), nil, "the bridge does not start at launch")
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    H.eq(#loadlib_calls, n + 1, "the first career event does not load it again")
    H.ok(read_json("bridge_state.json"), "the first career event starts the bridge (the DLL's other start signal)")
    H.eq(read_json("turbo_gui_load.json").mode, "launch", "mode file left alone")
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    H.eq(#loadlib_calls, n + 1, "only once")
    local boxes = #sim.boxes
    H.script("turbo_gui_load")
    H.ok(#sim.boxes > boxes, "load script reports")
    H.has(sim.boxes[#sim.boxes].text, "already loaded")
end)

H.case("turbo_gui_load.lua loads Turbo.dll in 'now' mode and says so; reports even a broken install", function()
    TURBO_STATE.bridge.gui_loaded = false
    local n = #loadlib_calls
    local boxes = #sim.boxes
    H.script("turbo_gui_load")
    H.eq(#loadlib_calls, n + 1, "loaded")
    H.eq(read_json("turbo_gui_load.json").mode, "now", "now mode: the game is running")
    H.has(sim.boxes[#sim.boxes].text, "Press F8")
    H.ok(#sim.boxes == boxes + 1, "one message box")
    local trace = H.read(H.out("turbo_boot.log"))
    H.has(trace, "turbo_gui_load.lua started")
    H.has(trace, "bridge.start: load_gui returned true")
    H.has(trace, "turbo_gui_load.lua finished")
    -- Turbo's library missing (wrong unzip): the script still reports instead of failing silently
    local lib = H.LE .. "/lua/libs/v2/imports/turbo/turbo.lua"
    assert(os.rename(lib, lib .. ".gone"))
    package.loaded["imports/turbo/turbo"] = nil
    local okr = pcall(H.script, "turbo_gui_load")
    assert(os.rename(lib .. ".gone", lib))
    package.loaded["imports/turbo/turbo"] = nil
    H.eq(okr, true, "the script itself does not raise")
    H.has(sim.boxes[#sim.boxes].text, "turbo_gui_load.lua failed")
    H.has(sim.boxes[#sim.boxes].text, "unzipped")
end)

local function deep_eq(a, b)
    if type(a) ~= type(b) then return false end
    if type(a) ~= "table" then return a == b end
    for k, v in pairs(a) do if not deep_eq(v, b[k]) then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
end

H.case("GetDBMeta returning C++ objects (userdata), iterable or index-only, gives the same bridge_meta.json", function()
    local bridge = require 'imports/turbo/bridge'
    sim.meta_mode = "table"
    local ok, err = bridge.write_meta(true)
    H.eq(ok, true, err)
    local plain = read_json("bridge_meta.json")
    H.ok(plain and next(plain.shortname_name_tables_map) ~= nil, "plain tables: written")
    for _, mode in ipairs({ "userdata", "userdata_noiter" }) do
        sim.meta_mode = mode
        os.remove(H.out("bridge_meta.json"))
        ok, err = bridge.write_meta(true)
        H.eq(ok, true, mode .. ": " .. tostring(err))
        H.eq(TURBO_STATE.bridge.meta_source, mode == "userdata" and "GetDBMeta" or "LE.db",
             mode .. ": read " .. (mode == "userdata" and "directly" or "through Live Editor's own t3db loader"))
        local got = read_json("bridge_meta.json") or {}
        H.ok(deep_eq(got.shortname_name_tables_map, plain.shortname_name_tables_map), mode .. ": same table names")
        H.ok(deep_eq(got.field_desc_map, plain.field_desc_map), mode .. ": same fields, depths and minimums")
    end
    sim.meta_mode = "table"
end)

H.case("a database that cannot be read is reported (message box, state file, trace), and retried on career events", function()
    local bridge = require 'imports/turbo/bridge'
    TURBO_STATE.bridge.meta_written = false   -- a fresh game session: no bridge_meta.json yet
    os.remove(H.out("bridge_meta.json"))
    local saved = GetDBMeta
    GetDBMeta = function() error("database not ready") end
    local boxes = #sim.boxes
    H.script("turbo_gui_load")
    GetDBMeta = saved
    H.ok(#sim.boxes > boxes, "the load script reports")
    local text = sim.boxes[#sim.boxes].text
    H.has(text, "Turbo GUI problem")
    H.has(text, "database could not be read")
    H.has(text, "database not ready")
    H.has(read_json("bridge_state.json").meta_error or "", "database not ready", "the GUI is told why")
    H.has(H.read(H.out("turbo_boot.log")), "bridge_meta.json NOT written")
    H.eq(TURBO_STATE.bridge.meta_written, false, "not marked written")
    sim:fire("post__CareerModeEvent", 0, 7, 0)
    H.eq(TURBO_STATE.bridge.meta_written, true, "written on the next career event")
    H.eq(read_json("bridge_state.json").meta_error, nil, "error cleared for the GUI")
    local ok, msg = bridge.start()
    H.eq(ok, true, msg)
    H.has(msg, "Game database shared with the Turbo GUI")
end)

H.case("meta is not written while the database is empty, and is written once it appears", function()
    local sim2 = H.setup({ in_cm = false })
    package.loadlib = function() return true end
    H.turbo().boot()
    require('imports/turbo/bridge').start()
    H.eq(H.read(H.out("bridge_meta.json")), nil, "no meta yet")
    W.build(sim2, {})
    sim2.in_cm = true
    sim2:fire("post__CareerModeEvent", 0, 7, 0)
    local meta = read_json("bridge_meta.json")
    H.ok(meta and meta.shortname_name_tables_map.plyr == "players", "meta written on the next event")
    local st = read_json("bridge_state.json")
    H.eq(st.db_service, string.format("0x%X", sim2.plugins[0x0ae932d0] - 8), "db service now known")
end)

package.loadlib = real_loadlib
H.finish()
