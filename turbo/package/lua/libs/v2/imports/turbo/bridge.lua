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

local util = require 'imports/turbo/core/util'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local game = require 'imports/turbo/core/game'

local M = {}

local MAGIC = 0x4F425254
local OFF_SEQ, OFF_ACK, OFF_STATUS, OFF_HEARTBEAT = 0x08, 0x0C, 0x10, 0x14
local OFF_CMD, OFF_RESULT, TEXT_SIZE = 0x20, 0x1020, 0x1000

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
function M.write_meta(force)
    if S.meta_written and not force then return true end
    if type(GetDBMeta) ~= "function" then return false, "GetDBMeta is not available" end
    local ok, meta = pcall(GetDBMeta)
    if not ok or type(meta) ~= "table" then return false, "GetDBMeta failed: " .. tostring(meta) end
    local names, fields = {}, {}
    for short, name in pairs(meta.shortname_name_tables_map or {}) do
        if type(short) == "string" and type(name) == "string" then names[short] = name end
    end
    for tshort, fmap in pairs(meta.field_desc_map or {}) do
        if type(tshort) == "string" and type(fmap) == "table" then
            local t = {}
            for fshort, d in pairs(fmap) do
                if type(fshort) == "string" and type(d) == "table" and type(d.name) == "string" then
                    t[fshort] = { name = d.name, depth = tonumber(d.depth) or 0, min = tonumber(d.min) or 0 }
                end
            end
            fields[tshort] = t
        end
    end
    if next(names) == nil or next(fields) == nil then return false, "database meta is empty (database not loaded yet)" end
    local j = json()
    local dir = M.dir()
    if not j or not dir then return false, "json library or output folder missing" end
    local okj, text = pcall(j.encode, { session = S.session, shortname_name_tables_map = names, field_desc_map = fields })
    if not okj then return false, "cannot encode meta: " .. tostring(text) end
    local okw, werr = util.write_file(util.join(dir, "bridge_meta.json"), text)
    if not okw then return false, tostring(werr) end
    S.meta_written = true
    return true
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

function M.collect_state()
    local in_cm = game.in_cm()
    local d = in_cm and game.current_date() or nil
    return {
        settings = S.settings,
        session = S.session,
        le_version = tostring(LE_VERSION or ""),
        db_service = hex(plugin("ENUM_djb2Database_CLSS") - 8),
        comm_service = hex(plugin("ENUM_djb2FeFceGMCommServiceInterface_CLSS")),
        ifce = hex(plugin("ENUM_djb2IFCEInterface_CLSS")),
        in_cm = in_cm,
        user_team = in_cm and game.user_team_id() or 0,
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
    local addr = tonumber((data.mailbox:gsub("^0[xX]", "")), 16)
    if not addr or addr < 0x10000 then return nil end
    addr = math.tointeger(addr)
    if MEMORY:ReadInt(addr) ~= MAGIC then return nil end
    return addr, data.session
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
        M.write_meta(true)
        M.write_state(true, true)
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
    if not S.meta_written then pcall(M.write_meta, false) end
    local okw, werr = pcall(M.write_state, false, reload_ids()[event_id] == true)
    if not okw then log.warn("bridge state: %s", tostring(werr)) end
    local okp, perr = pcall(M.poll_mailbox)
    if not okp then log.warn("bridge mailbox: %s", tostring(perr)) end
end

function M.on_le_init_done()
    M.write_meta(true)
    M.write_state(true, true)
end

-- ---------------------------------------------------------------- GUI loader
function M.gui_path()
    local root = env.le_root()
    if not root then return nil end
    return util.join(util.join(root, "turbo"), "Turbo.dll")
end

-- Load Turbo.dll into the game process through Lua's package.loadlib. Returns ok, message
function M.load_gui()
    if S.gui_loaded then return true, "Turbo GUI already loaded" end
    local path = M.gui_path()
    if not path or not util.file_exists(path) then return false, "Turbo.dll not found at " .. tostring(path) end
    if type(package) ~= "table" or type(package.loadlib) ~= "function" then
        return false, "this Live Editor build has no package.loadlib; run turbo\\TurboInjector.exe instead"
    end
    local f, err = package.loadlib(path, "*")
    if not f then return false, "package.loadlib failed: " .. tostring(err) end
    S.gui_loaded = true
    return true, "Turbo GUI loaded from " .. path
end

-- Called from TURBO.boot()
function M.start(cfg)
    M.set_settings(cfg)
    local ok_m, merr = pcall(M.write_meta, false)
    if not ok_m or merr == false then log.debug("bridge meta not written yet") end
    pcall(M.write_state, true, false)
    if type(AddEventHandler) == "function" and not S.init_registered then
        S.init_handler = S.init_handler or function() pcall(M.on_le_init_done) end
        pcall(AddEventHandler, "post__LEInitDoneEvent", S.init_handler)
        S.init_registered = true
    end
    local gui = cfg and cfg.gui or {}
    if gui.autoload ~= false then
        local ok, msg = M.load_gui()
        if ok then log.info("%s", msg) else log.warn("Turbo GUI not loaded: %s", msg) end
    end
end

return M
