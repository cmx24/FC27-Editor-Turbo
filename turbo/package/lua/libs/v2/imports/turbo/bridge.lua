-- FC 27 LE Turbo - bridge between Live Editor's Lua engine and the Turbo GUI (Turbo.dll).
--
-- Lua -> GUI  (files in <Live Editor>\turbo_output):
--   bridge_meta.json   database meta (GetDBMeta: table/field short names, bit depth, minimum)
--   bridge_state.json  DB / career plugin addresses, career state, in-game date
-- GUI -> Lua:
--   bridge_dll.json    address of the GUI's command mailbox in game memory
--   mailbox            polled on every career-mode event; commands run Turbo modules
--
-- Mailbox layout (allocated by Turbo.dll):
--   +0x00 magic 'TRBO'  +0x08 command seq  +0x0C ack seq  +0x10 status  +0x14 heartbeat
--   +0x20 command JSON (4096)  +0x1020 result text (4096)

-- LAUNCH SAFETY: at game launch only M.arm (pure Lua) and M.load_gui("launch") run. load_gui("launch") loads Turbo.dll,
-- which touches nothing until Live Editor reports "Initial setup done" in its log (or this Lua side runs in game).
-- The bridge itself (M.start: GetDBMeta, GetPlugin, game memory) starts only from turbo_gui_load.lua or on the
-- first career-mode event, when the game is fully running.

local util = require 'imports/turbo/core/util'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local game = require 'imports/turbo/core/game'
local trace = require 'imports/turbo/core/trace'

local M = {}

local MAGIC = 0x4F425254
local OFF_SEQ, OFF_ACK, OFF_STATUS, OFF_HEARTBEAT = 0x08, 0x0C, 0x10, 0x14
local OFF_CMD, OFF_RESULT, TEXT_SIZE = 0x20, 0x1020, 0x1000

-- bridge_dll.json is stamped by the DLL every ~2 s while it runs. A file older than this is left over from an
-- earlier game session: its mailbox address means nothing in this process and is never read.
local DLL_FRESH_SECONDS = 15

-- Event ids that mean "the database may have been replaced"
local RELOAD_EVENTS = { "POST_LOAD_PREPARE", "ENTERED_HUB_FIRST_TIME", "CAREER_TYPE_SELECTED", "INITIAL_USER_ADDED" }

TURBO_STATE = TURBO_STATE or { listeners = {} }
TURBO_STATE.bridge = TURBO_STATE.bridge or {
    seq = 0, db_gen = 0, last_state = nil, meta_written = false,
    mailbox = nil, mailbox_session = nil, next_dll_check = 0, heartbeat = 0,
    session = string.format("%X", math.floor(os.time() % 0x7FFFFFFF)),
    gui_loaded = false, reload_ids = nil,
}
local S = TURBO_STATE.bridge

local function json()
    local ok, j = pcall(require, 'imports/external/json')
    if ok and type(j) == "table" then return j end
    return nil
end

-- Folder for bridge files: <root>\turbo_output when writable, else <root>
function M.dir()
    local root = env.le_root()
    if not root then return nil end
    local out = util.join(root, "turbo_output")
    if util.write_file(util.join(out, ".bridge_write_test"), "ok") then
        os.remove(util.join(out, ".bridge_write_test"))
        return out
    end
    return root
end

local function hex(v)
    if type(v) ~= "number" or v <= 0 then return "0x0" end
    return string.format("0x%X", math.tointeger(v) or 0)
end

local function plugin(enum_name)
    pcall(require, 'imports/services/enums')
    local id = _G[enum_name]
    if type(GetPlugin) ~= "function" or type(id) ~= "number" then return 0 end
    local ok, addr = pcall(GetPlugin, id)
    if ok and type(addr) == "number" then return math.tointeger(addr) or 0 end
    return 0
end

-- ---------------------------------------------------------------- Lua -> GUI
-- GetDBMeta() may return plain Lua tables or C++ objects (userdata). Live Editor's own t3db code only indexes it
-- (meta.shortname_name_tables_map[short], fld_meta[short].name / .depth / .min), so Turbo never relies on its Lua type.
local function desc_entry(d)
    local name = util.index(d, "name")
    if type(name) ~= "string" or name == "" then return nil end
    return { name = name, depth = tonumber(util.index(d, "depth")) or 0, min = tonumber(util.index(d, "min")) or 0 }
end

-- 1) Iterate the meta maps (Lua tables, or userdata containers that support pairs)
local function meta_by_iteration(meta)
    local names, fields = {}, {}
    local ok1 = util.each(util.index(meta, "shortname_name_tables_map"), function(short, name)
        if type(short) == "string" and type(name) == "string" then names[short] = name end
    end)
    local ok2 = util.each(util.index(meta, "field_desc_map"), function(tshort, fmap)
        if type(tshort) ~= "string" then return end
        local t = {}
        util.each(fmap, function(fshort, d)
            if type(fshort) == "string" then t[fshort] = desc_entry(d) end
        end)
        if next(t) ~= nil then fields[tshort] = t end
    end)
    if not ok1 or not ok2 then return nil, nil end
    return names, fields
end

-- 2) The tables in game memory, loaded by Live Editor's own t3db library (LE.db), which looks each short name up in the
-- meta by indexing only. Used when the meta cannot be iterated.
local function meta_from_le_db()
    if type(LE) ~= "table" or type(LE.db) ~= "table" or type(LE.db.Reset) ~= "function" then return nil, nil end
    local ok = pcall(function() LE.db:Reset() end)
    if not ok or type(LE.db.tables) ~= "table" then return nil, nil end
    local names, fields = {}, {}
    for name, tbl in pairs(LE.db.tables) do
        local short = type(tbl) == "table" and tbl.shortname or nil
        if type(name) == "string" and type(short) == "string" and short ~= "" then
            names[short] = name
            local t = {}
            if type(tbl.fields) == "table" then
                for _, fld in pairs(tbl.fields) do
                    if type(fld) == "table" and type(fld.shortname) == "string" then t[fld.shortname] = desc_entry(fld.fld_desc) end
                end
            end
            fields[short] = t
        end
    end
    return names, fields
end

function M.write_meta(force)
    if S.meta_written and not force then return true end
    if type(GetDBMeta) ~= "function" then return false, "GetDBMeta is not available" end
    local ok, meta = pcall(GetDBMeta)
    if not ok then return false, "GetDBMeta failed: " .. tostring(meta) end
    if not util.is_object(meta) then return false, "GetDBMeta returned " .. type(meta) end
    local names, fields = meta_by_iteration(meta)
    local source = "GetDBMeta"
    if not names or next(names) == nil or next(fields) == nil then
        names, fields = meta_from_le_db()
        source = "LE.db"
    end
    if not names or next(names) == nil or next(fields) == nil then
        return false, "the database meta is empty (database not loaded yet?)"
    end
    local j = json()
    local dir = M.dir()
    if not j or not dir then return false, "json library or output folder missing" end
    local okj, text = pcall(j.encode, { session = S.session, shortname_name_tables_map = names, field_desc_map = fields })
    if not okj then return false, "cannot encode meta: " .. tostring(text) end
    local okw, werr = util.write_file(util.join(dir, "bridge_meta.json"), text)
    if not okw then return false, tostring(werr) end
    S.meta_written = true
    S.meta_source = source
    return true
end

-- Player names. playernames.name is a compressed text field (field type 13) in FC 27: Turbo.dll cannot read it from
-- memory, but Live Editor's documented GetDBTableRows decodes it (43,000 names in ~0.3 s). The names go to
-- bridge_names.txt: first line "#turbo-names <session> <count>", then one "nameid<TAB>name" line per name.
M.NAME_TABLES = { "playernames" }

local function row_value(row, key)
    local cell = util.index(row, key)
    if util.is_object(cell) then cell = util.index(cell, "value") end
    return cell
end

function M.write_names()
    local get_rows = _G["GetDBTableRows"]
    if type(get_rows) ~= "function" then return false, "GetDBTableRows is not available" end
    local dir = M.dir()
    if not dir then return false, "output folder missing" end
    local out, count = {}, 0
    for _, tname in ipairs(M.NAME_TABLES) do
        local ok, rows = pcall(get_rows, tname)
        if ok and util.is_object(rows) then
            for i = 1, util.len(rows) do
                local row = util.index(rows, i)
                local id = tonumber(row_value(row, "nameid"))
                local name = row_value(row, "name")
                if id and type(name) == "string" and name ~= "" then
                    out[#out + 1] = string.format("%d\t%s", math.tointeger(id) or 0, (name:gsub("[\t\r\n]", " ")))
                    count = count + 1
                end
            end
        end
    end
    if count == 0 then return false, "no player names returned by GetDBTableRows" end
    local text = string.format("#turbo-names %s %d\n", S.session, count) .. table.concat(out, "\n") .. "\n"
    local okw, werr = util.write_file(util.join(dir, "bridge_names.txt"), text)
    if not okw then return false, tostring(werr) end
    S.names_count = count
    pcall(M.write_commentary_names)
    return true, count
end

-- Commentary names ("callnames"): commentarynames.commentarystring is compressed text too. The ids and their text go to
-- bridge_commentary.txt: "#turbo-commentary <session> <count>", then "commentaryid<TAB>text" lines. Which of these
-- ids are spoken in the commentary language the game has loaded is decided on the GUI side (per-language lists).
function M.write_commentary_names()
    local get_rows = _G["GetDBTableRows"]
    if type(get_rows) ~= "function" then return false, "GetDBTableRows is not available" end
    local dir = M.dir()
    if not dir then return false, "output folder missing" end
    local ok, rows = pcall(get_rows, "commentarynames")
    if not ok or not util.is_object(rows) then return false, "commentarynames not readable" end
    local out, count = {}, 0
    for i = 1, util.len(rows) do
        local row = util.index(rows, i)
        local id = tonumber(row_value(row, "commentaryid"))
        local text = row_value(row, "commentarystring")
        if id and type(text) == "string" and text ~= "" then
            out[#out + 1] = string.format("%d\t%s", math.tointeger(id) or 0, (text:gsub("[\t\r\n]", " ")))
            count = count + 1
        end
    end
    if count == 0 then return false, "no commentary names" end
    local body = string.format("#turbo-commentary %s %d\n", S.session, count) .. table.concat(out, "\n") .. "\n"
    local okw, werr = util.write_file(util.join(dir, "bridge_commentary.txt"), body)
    if not okw then return false, tostring(werr) end
    S.commentary_count = count
    return true, count
end

-- force = retry now even if the last attempt failed less than a minute ago
local function try_write_names(force)
    if not force and S.names_retry_at and os.time() < S.names_retry_at then return false, "retry later" end
    local okc, ok, res = pcall(M.write_names)
    if not okc then ok, res = false, tostring(ok) end
    if ok then
        S.names_retry_at = nil
    else
        S.names_retry_at = os.time() + 60
        log.warn("bridge_names.txt not written: %s", tostring(res))
    end
    return ok, res
end

-- write_meta, remembering why it failed (shown by the GUI through bridge_state.json and reported by bridge.start)
local function try_write_meta(force)
    local okc, ok, err = pcall(M.write_meta, force)
    if not okc then ok, err = false, tostring(ok) end
    if ok then
        S.meta_error = nil
        return true
    end
    err = tostring(err or "unknown error")
    if S.meta_error ~= err then log.warn("bridge_meta.json not written: %s", err) end
    -- the GUI is told only while it has no bridge_meta.json from this session to work with
    if not S.meta_written then S.meta_error = err end
    return false, err
end

-- Effective Turbo settings (turbo_config.json + gui_settings.json) shown by the GUI
function M.set_settings(cfg)
    if type(cfg) ~= "table" then return end
    local a = type(cfg.auto) == "table" and cfg.auto or {}
    local fm = type(a.form_morale) == "table" and a.form_morale or {}
    local ps = type(a.pap_playstyles) == "table" and a.pap_playstyles or {}
    S.settings = {
        dry_run = type(cfg.turbo) == "table" and cfg.turbo.dry_run == true,
        auto = {
            form_morale = { enabled = fm.enabled == true, form = tonumber(fm.form) or 0,
                            morale = tonumber(fm.morale) or 0, fitness = tonumber(fm.fitness) or 0 },
            pap_playstyles = { enabled = ps.enabled == true },
        },
    }
end

local function transfer_budget()
    local ok, tb = pcall(require, 'imports/turbo/features/transfer_budget')
    if not ok then return nil end
    return (tb.current())
end

-- Tools this Live Editor build cannot run (fixed for a Live Editor build, so worked out once)
local function unavailable_tools()
    if S.unavailable == nil then
        local ok, caps = pcall(require, 'imports/turbo/core/caps')
        local okc, list = false, nil
        if ok then okc, list = pcall(caps.unavailable) end
        S.unavailable = (okc and type(list) == "table") and list or {}
    end
    return S.unavailable
end

-- Moves Turbo makes itself in this Live Editor build (worked out once)
local function turbo_made_tools()
    if S.turbo_made == nil then
        local ok, caps = pcall(require, 'imports/turbo/core/caps')
        local okc, list = false, nil
        if ok then okc, list = pcall(caps.turbo_made) end
        S.turbo_made = (okc and type(list) == "table") and list or {}
    end
    return S.turbo_made
end

function M.collect_state()
    local in_cm = game.in_cm()
    local d = in_cm and game.current_date() or nil
    return {
        settings = S.settings,
        session = S.session,
        meta_error = S.meta_error,
        names_count = S.names_count or 0,
        commentary_count = S.commentary_count or 0,
        le_version = tostring(LE_VERSION or ""),
        db_service = hex(plugin("ENUM_djb2Database_CLSS") - 8),
        comm_service = hex(plugin("ENUM_djb2FeFceGMCommServiceInterface_CLSS")),
        ifce = hex(plugin("ENUM_djb2IFCEInterface_CLSS")),
        in_cm = in_cm,
        user_team = in_cm and game.user_team_id() or 0,
        transfer_budget = in_cm and transfer_budget() or nil,
        unavailable = unavailable_tools(),
        turbo_made = turbo_made_tools(),
        date = d and { year = d.year, month = d.month, day = d.day } or nil,
    }
end

local function same_settings(a, b)
    local j = json()
    if not j then return true end
    local ok1, ea = pcall(j.encode, a or {})
    local ok2, eb = pcall(j.encode, b or {})
    return ok1 and ok2 and ea == eb
end

local function same_state(a, b)
    if not a or not b then return false end
    if a.settings ~= b.settings and not same_settings(a.settings, b.settings) then return false end
    if a.db_service ~= b.db_service or a.in_cm ~= b.in_cm or a.user_team ~= b.user_team then return false end
    if a.transfer_budget ~= b.transfer_budget then return false end
    if a.meta_error ~= b.meta_error then return false end
    local ad, bd = a.date, b.date
    if (ad == nil) ~= (bd == nil) then return false end
    if ad and (ad.year ~= bd.year or ad.month ~= bd.month or ad.day ~= bd.day) then return false end
    return true
end

-- Write bridge_state.json when something changed (or force). reload = the database may have moved.
function M.write_state(force, reload)
    local st = M.collect_state()
    local prev = S.last_state
    local structural = reload or not prev or prev.db_service ~= st.db_service or prev.in_cm ~= st.in_cm
        or prev.user_team ~= st.user_team
    if not force and not structural and same_state(prev, st) then return true end
    if structural then S.db_gen = S.db_gen + 1 end
    S.seq = S.seq + 1
    st.seq = S.seq
    st.db_gen = S.db_gen
    local j = json()
    local dir = M.dir()
    if not j or not dir then return false, "json library or output folder missing" end
    local ok, text = pcall(j.encode, st)
    if not ok then return false, tostring(text) end
    local okw, werr = util.write_file(util.join(dir, "bridge_state.json"), text)
    if not okw then return false, tostring(werr) end
    S.last_state = st
    return true
end

-- ---------------------------------------------------------------- GUI -> Lua
local function find_mailbox()
    local dir = M.dir()
    local j = json()
    if not dir or not j then return nil end
    local path = util.join(dir, "bridge_dll.json")
    if not util.file_exists(path) then return nil end
    local ok, data = pcall(j.decode, util.read_file(path) or "")
    if not ok or type(data) ~= "table" or type(data.mailbox) ~= "string" then return nil end
    -- Never touch memory on the word of a stale file (a previous game session wrote it)
    local updated = tonumber(data.updated)
    if not updated or math.abs(os.time() - updated) > DLL_FRESH_SECONDS then return nil end
    local addr = tonumber((data.mailbox:gsub("^0[xX]", "")), 16)
    if not addr or addr < 0x10000 then return nil end
    addr = math.tointeger(addr)
    if MEMORY:ReadInt(addr) ~= MAGIC then return nil end
    return addr, data.session
end

-- Address of the Turbo GUI's mailbox in this game process (nil without a fresh bridge_dll.json); used by core/mem.lua
-- to find the readable-memory map
function M.mailbox_address()
    if S.mailbox and MEMORY:ReadInt(S.mailbox) == MAGIC then return S.mailbox end
    local addr = find_mailbox()
    if addr then S.mailbox = addr end
    return addr
end

local function read_text(addr)
    local s = MEMORY:ReadString(addr, TEXT_SIZE)
    if type(s) ~= "string" then return "" end
    return (s:match("^[^%z]*"))
end

-- Execute one decoded command; returns ok, text
function M.execute(cmd)
    if type(cmd) ~= "table" then return false, "bad command" end
    local TURBO = require 'imports/turbo/turbo'
    if cmd.op == "ping" then return true, "pong" end
    if cmd.op == "boot" then
        local enabled = TURBO.boot()
        return true, "automatic features: " .. (#enabled > 0 and table.concat(enabled, ", ") or "none")
    end
    if cmd.op == "run" then
        if type(cmd.module) ~= "string" then return false, "module missing" end
        local overrides = type(cmd.overrides) == "table" and cmd.overrides or nil
        return TURBO.run(cmd.module, overrides, { silent = true })
    end
    if cmd.op == "refresh" then
        local okm, merr = try_write_meta(true)
        try_write_names(true)
        M.write_state(true, true)
        if not okm then return false, "bridge_meta.json not written: " .. tostring(merr) end
        return true, "bridge refreshed"
    end
    return false, "unknown op " .. tostring(cmd.op)
end

local function clock()
    if type(os) == "table" and type(os.clock) == "function" then return os.clock() end
    return 0
end

-- Check the mailbox; runs at most one pending command. Returns true when a command ran.
-- force = look for bridge_dll.json now instead of at most once a second
function M.poll_mailbox(force)
    if not S.mailbox then
        local t = clock()
        if not force and t < S.next_dll_check then return false end
        S.next_dll_check = t + 1.0
        local addr, session = find_mailbox()
        if not addr then return false end
        S.mailbox, S.mailbox_session = addr, session
        log.info("connected to Turbo GUI mailbox at 0x%X", addr)
    end
    local mb = S.mailbox
    if MEMORY:ReadInt(mb) ~= MAGIC then
        S.mailbox = nil
        return false
    end
    S.heartbeat = (S.heartbeat + 1) % 0x7FFFFFFF
    MEMORY:WriteInt(mb + OFF_HEARTBEAT, S.heartbeat)
    local seq = MEMORY:ReadInt(mb + OFF_SEQ)
    if seq == MEMORY:ReadInt(mb + OFF_ACK) then return false end

    local ok, text
    local j = json()
    local okd, cmd = pcall(j.decode, read_text(mb + OFF_CMD))
    if not okd then
        ok, text = false, "command is not valid JSON"
    else
        local okx, r1, r2 = pcall(M.execute, cmd)
        if okx then ok, text = r1 == true, tostring(r2 or "") else ok, text = false, "error: " .. tostring(r1) end
    end
    if #text > TEXT_SIZE - 1 then text = text:sub(1, TEXT_SIZE - 1) end
    MEMORY:WriteString(mb + OFF_RESULT, text)
    MEMORY:WriteInt(mb + OFF_STATUS, ok and 1 or 0)
    MEMORY:WriteInt(mb + OFF_ACK, seq)
    return true
end

-- ---------------------------------------------------------------- events
local function reload_ids()
    if S.reload_ids then return S.reload_ids end
    local events = require 'imports/turbo/core/events'
    S.reload_ids = events.resolve_set(RELOAD_EVENTS)
    return S.reload_ids
end

function M.on_career_event(event_id)
    if S.autoload_pending then
        -- gui.autoload: first career-mode event = the game is fully running; start the bridge now
        S.autoload_pending = false
        local ok, err = pcall(M.start, S.cfg)
        if not ok then log.error("Turbo GUI start failed: %s", tostring(err)) end
        return
    end
    if not S.meta_written then try_write_meta(false) end
    local reload = reload_ids()[event_id] == true
    if reload or not S.names_count then try_write_names() end
    local okw, werr = pcall(M.write_state, false, reload)
    if not okw then log.warn("bridge state: %s", tostring(werr)) end
    local okp, perr = pcall(M.poll_mailbox)
    if not okp then log.warn("bridge mailbox: %s", tostring(perr)) end
    -- game images the Turbo window asked for (core/legacy.lua): a quarter of a second per event
    local okl, lerr = pcall(function() return (require 'imports/turbo/core/legacy').pump(0.25) end)
    if not okl then log.warn("bridge images: %s", tostring(lerr)) end
end

-- ---------------------------------------------------------------- GUI loader
function M.gui_path()
    local root = env.le_root()
    if not root then return nil end
    return util.join(util.join(root, "turbo"), "Turbo.dll")
end

-- Load Turbo.dll into the game process through Lua's package.loadlib. Returns ok, message.
-- mode "launch" (lua\autorun, the game is starting): Turbo.dll waits until Live Editor reports "Initial setup done" in its
-- log or this Lua side runs in game. mode "now" (default: turbo_gui_load.lua, career event): the game is running.
-- The mode is handed over in turbo_output\turbo_gui_load.json, written right before the DLL is loaded.
function M.load_gui(mode)
    if S.gui_loaded then return true, "Turbo GUI already loaded" end
    mode = mode == "launch" and "launch" or "now"
    local path = M.gui_path()
    if not path or not util.file_exists(path) then return false, "Turbo.dll not found at " .. tostring(path) end
    if type(package) ~= "table" or type(package.loadlib) ~= "function" then
        return false, "this Live Editor build has no package.loadlib; run turbo\\TurboInjector.exe instead"
    end
    local dir, j = M.dir(), json()
    if dir and j then
        local okj, text = pcall(j.encode, { mode = mode, time = os.time() })
        if okj then util.write_file(util.join(dir, "turbo_gui_load.json"), text) end
    end
    local f, err = package.loadlib(path, "*")
    if not f then return false, "package.loadlib failed: " .. tostring(err) end
    S.gui_loaded = true
    return true, "Turbo GUI loaded from " .. path
end

-- gui.autoload: remember the settings and start the bridge on the first career-mode event (pure Lua, safe at launch)
function M.arm(cfg)
    local events = require 'imports/turbo/core/events'
    S.cfg = cfg
    if S.started then return end
    S.autoload_pending = true
    events.set_tap("bridge", M.on_career_event)
    trace.step("bridge armed: connects to the game database on the first career-mode event")
end

-- Start the bridge and load the Turbo GUI. Call it only while the game is running (turbo_gui_load.lua).
-- Every step is logged to turbo_output\turbo_boot.log before it runs. Returns ok, message
function M.start(cfg)
    local events = require 'imports/turbo/core/events'
    if type(cfg) ~= "table" then cfg = (require 'imports/turbo/core/config').load() end
    S.cfg = cfg
    S.autoload_pending = false
    S.started = true
    trace.step("bridge.start: begin")
    M.set_settings(cfg)
    events.set_tap("bridge", M.on_career_event)
    local okr, rerr = events.ensure_registered()
    if not okr then log.warn("cannot register career events: %s", tostring(rerr)) end
    trace.step("bridge.start: writing bridge_meta.json (GetDBMeta)")
    local ok_m, merr = try_write_meta(true)
    trace.step("bridge.start: bridge_meta.json " .. (ok_m and ("written from " .. tostring(S.meta_source)) or ("NOT written: " .. tostring(merr))))
    trace.step("bridge.start: writing bridge_names.txt (GetDBTableRows playernames)")
    local ok_n, nres = try_write_names(true)
    trace.step("bridge.start: bridge_names.txt " .. (ok_n and ("written, " .. tostring(nres) .. " names") or ("NOT written: " .. tostring(nres))))
    trace.step("bridge.start: writing bridge_state.json (GetPlugin)")
    local okc, ok_s, serr = pcall(M.write_state, true, false)
    if not okc then ok_s, serr = false, tostring(ok_s) end
    trace.step("bridge.start: bridge_state.json " .. (ok_s and "written" or ("NOT written: " .. tostring(serr))))
    trace.step("bridge.start: loading turbo\\Turbo.dll (package.loadlib)")
    local ok, msg = M.load_gui()
    trace.step("bridge.start: load_gui returned " .. tostring(ok) .. " - " .. tostring(msg))
    if ok then log.info("%s", msg) else log.warn("Turbo GUI not loaded: %s", msg) end
    if not ok then return false, msg end
    if not ok_m then
        return false, msg .. "\nBut the game database could not be read: " .. tostring(merr)
            .. "\nSee turbo_output\\turbo_boot.log; send it with Live Editor's log."
    end
    if not ok_s then
        return false, msg .. "\nBut bridge_state.json could not be written: " .. tostring(serr)
    end
    return true, msg .. "\nGame database shared with the Turbo GUI (" .. tostring(S.meta_source) .. ")."
end

return M
