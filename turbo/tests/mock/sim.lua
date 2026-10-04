-- Simulated Live Editor runtime for offline tests.
-- Byte-addressable memory with page mapping, the native functions FC 27 LE exposes to Lua,
-- and builders for T3DB tables and FCE career-mode managers laid out exactly as the real
-- Live Editor Lua libraries (t3db/*.lua, career_mode/helpers.lua) expect.

local Sim = {}
Sim.__index = Sim

local PAGE = 12

-- A C++ object the way a native binding may hand it to Lua: a full userdata whose only behaviour is its metatable
-- (a closed file handle given a new metatable; pure Lua cannot create userdata otherwise).
local function make_userdata(mt)
    local u = io.tmpfile()
    u:close()
    debug.setmetatable(u, mt)
    return u
end

-- Userdata view of a Lua table: indexable; iterable (pairs, #) only when iterable is true
local function proxy(t, iterable)
    local mt = { __index = function(_, k) return t[k] end, __name = "sol.native_object" }
    if iterable then
        mt.__pairs = function() return next, t, nil end
        mt.__len = function() return #t end
    end
    return make_userdata(mt)
end
Sim.proxy = proxy

function Sim.new()
    local s = setmetatable({}, Sim)
    s.mem = {}
    s.pages = {}
    s.next_alloc = 0x20000000
    s.unmapped_reads = 0
    s.logs = {}
    s.boxes = {}
    s.calls = {}
    s.handlers = {}
    s.plugins = {}
    s.in_cm = false
    s.meta = { shortname_name_tables_map = {}, field_desc_map = {} }
    -- What GetDBMeta returns: "table" (plain Lua tables), "userdata" (C++ objects whose maps support pairs) or
    -- "userdata_noiter" (C++ objects that can only be indexed, like Live Editor's own t3db code uses them)
    s.meta_mode = "table"
    s.tables = {}
    s.managers = {}
    s.bans = {}
    s.stats = {}
    s.team_names = {}
    s.legacy_files = {}
    s.date = { day = 15, month = 1, year = 2027 }
    return s
end

------------------------------------------------------------------ memory
function Sim:alloc(size, align)
    align = align or 16
    local a = (self.next_alloc + align - 1) // align * align
    self.next_alloc = a + size + 0x40
    for p = a >> PAGE, (a + size + 16) >> PAGE do self.pages[p] = true end
    if self.gui and not self.publishing then self:publish_map() end
    return a
end

-- What Turbo.dll does in game (turbogui/src/core/memmap.h): a mailbox whose +0x18 points to the readable-memory map,
-- republished whenever the simulated process maps memory. le_dir: bridge_dll.json is written there.
local MAP_CAPACITY = 8192
function Sim:enable_gui(le_dir)
    self.publishing = true
    local mb = self:alloc(0x2300, 16)   -- turbo::kMailboxSize (version 2: game-call block at +0x2020)
    local map = self:alloc(0x20 + MAP_CAPACITY * 16, 16)
    self.publishing = false
    self:w32(mb, 0x4F425254)
    self:w32(mb + 4, 2)
    self:w64(mb + 0x18, map)
    self.gui = { mailbox = mb, map = map }
    self:publish_map()
    if le_dir then
        local f = assert(io.open(le_dir .. "/turbo_output/bridge_dll.json", "wb"))
        f:write(string.format('{"mailbox":"0x%X","session":"sim","gui_version":"sim","updated":%d}', mb, os.time()))
        f:close()
    end
    return mb, map
end

function Sim:publish_map()
    local map = self.gui.map
    local pages = {}
    for p in pairs(self.pages) do pages[#pages + 1] = p end
    table.sort(pages)
    local regions = {}
    for _, p in ipairs(pages) do
        local start, stop = p << PAGE, (p + 1) << PAGE
        local last = regions[#regions]
        if last and last[2] == start then last[2] = stop else regions[#regions + 1] = { start, stop } end
    end
    assert(#regions <= MAP_CAPACITY, "sim: too many regions for the map")
    self.publishing = true
    local seq = self:r32(map + 8)
    self:w32(map + 8, seq + 1)   -- odd: being written
    for i, r in ipairs(regions) do
        self:w64(map + 0x20 + (i - 1) * 16, r[1])
        self:w64(map + 0x20 + (i - 1) * 16 + 8, r[2])
    end
    self:w32(map, 0x4D4D5254)
    self:w32(map + 4, 1)
    self:w32(map + 0x0C, #regions)
    self:w32(map + 0x10, MAP_CAPACITY)
    self:w32(map + 8, seq + 2)   -- even: complete
    self.publishing = false
end

function Sim:mapped(addr)
    return math.type(addr) == "integer" and addr >= 0 and self.pages[addr >> PAGE] == true
end

function Sim:rb(addr)
    if not self:mapped(addr) then
        self.unmapped_reads = self.unmapped_reads + 1
        return 0
    end
    return self.mem[addr] or 0
end

function Sim:wb(addr, v)
    if not self:mapped(addr) then error(string.format("write to unmapped address 0x%X", addr), 2) end
    self.mem[addr] = v & 0xFF
end

function Sim:ru(addr, n)
    local v = 0
    for i = n - 1, 0, -1 do v = (v << 8) | self:rb(addr + i) end
    return v
end

function Sim:wu(addr, n, v)
    for i = 0, n - 1 do self:wb(addr + i, (v >> (8 * i)) & 0xFF) end
end

local function signed(v, bits)
    local lim = 1 << (bits - 1)
    if v >= lim then return v - (1 << bits) end
    return v
end

function Sim:r8(a) return self:ru(a, 1) end
function Sim:r16(a) return signed(self:ru(a, 2), 16) end
function Sim:r32(a) return signed(self:ru(a, 4), 32) end
function Sim:r64(a) return self:ru(a, 8) end
function Sim:w8(a, v) self:wu(a, 1, v) end
function Sim:w16(a, v) self:wu(a, 2, v) end
function Sim:w32(a, v) self:wu(a, 4, v) end
function Sim:w64(a, v) self:wu(a, 8, v) end

function Sim:wstr(a, s)
    for i = 1, #s do self:wb(a + i - 1, s:byte(i)) end
    self:wb(a + #s, 0)
end

------------------------------------------------------------------ natives
function Sim:record(name, ...)
    self.calls[name] = self.calls[name] or {}
    table.insert(self.calls[name], { ... })
end

-- The natives FC 27 Live Editor v27.1.2 really has (globals dump from the game, 02-10-2026): every other native the
-- simulator installs is removed, so the Lua wrappers of lua\libs\v1\live_editor.lua (TransferPlayer ->
-- cTransferPlayer, ...) point at nothing, as they do in that build.
Sim.LE_27_1_2_REMOVED = {
    "GetUserTransferBudget", "SetUserTransferBudget", "GetCPUTransferBudget", "SetCPUTransferBudget",
    "GetCurrentDate", "GetTeamIdFromPlayerId", "GetCompetitionNameByObjID", "GetCompetitionNameByID",
    "GetPlayersStats", "PlayerExists", "DeletePlayer", "TerminateLoan",
    "cTransferPlayer", "cLoanPlayer", "cReleasePlayer", "cIsPlayerTransferListed", "cIsPlayerLoanListed",
    "cAddPlayerToTransferList", "cAddPlayerToLoanList", "cRemovePlayerFromLists", "cRemovePlayerFromTransferList",
    "cRemovePlayerFromLoanList", "cGetTransferBans", "cAddTransferBan", "cRemoveTransferBan", "cSaveTransferBans",
    "PlayerDevelopmentManagerLoad", "PlayerDevelopmentManagerSave", "PlayerDevelopmentManagerAddPlayer",
    "PlayerDevelopmentManagerRemovePlayer",
}

function Sim:as_le_27_1_2()
    for _, name in ipairs(Sim.LE_27_1_2_REMOVED) do _G[name] = nil end
    LE_VERSION = "v27.1.2"
end

function Sim:count_calls(name)
    return self.calls[name] and #self.calls[name] or 0
end

function Sim:install()
    local s = self
    -- memory
    ReadBytes = function(a, n) local t = {} for i = 1, n do t[i] = s:rb(a + i - 1) end return t end
    ReadShort = function(a) return s:r16(a) end
    ReadInteger = function(a) return s:r32(a) end
    ReadQword = function(a) return s:r64(a) end
    ReadFloat = function(a) return (string.unpack("<f", string.pack("<I4", s:ru(a, 4)))) end
    ReadString = function(a, n)
        local out = {}
        n = n or 1024
        for i = 0, n - 1 do
            local b = s:rb(a + i)
            if b == 0 then break end
            out[#out + 1] = string.char(b)
        end
        return table.concat(out)
    end
    WriteBytes = function(a, t) for i, b in ipairs(t) do s:wb(a + i - 1, b) end end
    WriteShort = function(a, v) s:w16(a, v) end
    WriteInteger = function(a, v) s:w32(a, v) end
    WriteQword = function(a, v) s:w64(a, v) end
    WriteFloat = function(a, v) s:wu(a, 4, (string.unpack("<I4", string.pack("<f", v)))) end
    WriteString = function(a, str) s:wstr(a, str) end
    AOBScan = function() return 0 end
    AllocateMemory = function(_, size) return s:alloc(size) end
    DeallocateMemory = function() return true end
    WriteJMP = function(...) s:record("WriteJMP", ...) end

    -- core
    Log = function(text, level) table.insert(s.logs, { text = tostring(text), level = level or 1 }) end
    -- Live Editor formats message box text like printf: a lone "%" crashes the game (FC 27 LE v27.1.2). The sim records
    -- such a call as a violation and shows the text the way Live Editor would ("%%" -> "%").
    MessageBox = function(title, text)
        local function shown(v)
            local raw = tostring(v)
            if raw:gsub("%%%%", ""):find("%%") then s.box_format_violations = (s.box_format_violations or 0) + 1 end
            return (raw:gsub("%%%%", "%%"))
        end
        table.insert(s.boxes, { title = shown(title), text = shown(text), raw = text })
    end
    IsInCM = function() return s.in_cm end
    GetPlugin = function(hash) return s.plugins[hash] or 0 end
    GetDBMeta = function() return s:db_meta() end
    GetSaveUID = function() return "TESTSAVE" end
    -- FC 27 LE natives (its v1 lib turns GetTransferBudget / SetTransferBudget into deprecation stubs)
    GetUserTransferBudget = function() return s.in_cm and (s.transfer_budget or 0) or 0 end
    SetUserTransferBudget = function(v)
        s:record("SetUserTransferBudget", v)
        if s.in_cm then s.transfer_budget = v end
    end
    -- Live Editor's documented GetDBTableRows: every valid row, each field as { value = <text> }; decodes compressed text
    GetDBTableRows = function(name)
        s:record("GetDBTableRows", name)
        local t = s.tables[name]
        local out = {}
        if not t then return out end
        for _, rec in ipairs(s:rows(name)) do
            local row = { addr = string.format("%X", rec) }
            for _, c in ipairs(t.cols) do row[c.name] = { value = tostring(s:get_field(rec, c)) } end
            out[#out + 1] = row
        end
        return out
    end

    -- FC 27 LE: insert a row (fields as text) into a free record; returns the row like GetDBTableRows does
    InsertDBTableRow = function(name, row)
        s:record("InsertDBTableRow", name, row)
        local t = s.tables[name]
        if not t then error("no table " .. tostring(name)) end
        for i = 0, t.n - 1 do
            local rec = t.first + i * t.rec_size
            if (s:rb(rec + t.rec_size - 1) & 0x80) ~= 0 then
                for k = 0, t.rec_size - 2 do s:wb(rec + k, 0) end
                for _, c in ipairs(t.cols) do
                    local v = row[c.name]
                    if v ~= nil then s:set_field(rec, c, (c.typ == 0) and tostring(v) or tonumber(v)) end
                end
                s:wb(rec + t.rec_size - 1, 0)
                local out = {}
                for _, c in ipairs(t.cols) do
                    out[c.name] = { value = tostring(s:get_field(rec, c)), addr = string.format("%d", rec) }
                end
                return out
            end
        end
        error("table " .. name .. " is full")
    end
    -- FC 27 LE: the record address as a decimal string
    DeleteDBTableRowByAddr = function(name, addr)
        s:record("DeleteDBTableRowByAddr", name, addr)
        local t = s.tables[name]
        local rec = math.tointeger(tonumber(addr))
        if not t or not rec or rec < t.first or rec >= t.first + t.n * t.rec_size or (rec - t.first) % t.rec_size ~= 0 then
            error("bad record address " .. tostring(addr))
        end
        s:wb(rec + t.rec_size - 1, 0x80)
        return true
    end
    -- Legacy files (s.legacy_files: path -> bytes)
    LegacyFileExist = function(p) return s.legacy_files[p] ~= nil end
    LegacyFileExport = function(p, dest)
        s:record("LegacyFileExport", p, dest)
        local data = s.legacy_files[p]
        if not data then return false end
        os.execute(string.format("mkdir -p '%s'", dest:match("(.*)/") or "."))
        local f = io.open(dest, "wb")
        if not f then return false end
        f:write(data)
        f:close()
        return true
    end

    -- events
    AddEventHandler = function(ev, fn)
        s.handlers[ev] = s.handlers[ev] or {}
        for _, h in ipairs(s.handlers[ev]) do if h.fn == fn then return end end
        s.next_handler_id = (s.next_handler_id or 0) + 1
        table.insert(s.handlers[ev], { id = s.next_handler_id, fn = fn })
    end
    GetEventHandlers = function(ev) return s.handlers[ev] or {} end
    RemoveEventHandler = function(ev, id)
        for i, h in ipairs(s.handlers[ev] or {}) do if h.id == id then table.remove(s.handlers[ev], i) return end end
    end
    ClearEventHandlersForEvent = function(ev) s.handlers[ev] = {} end

    -- API v1
    -- documented v1 native (DOC.MD); FC 27 LE's career_mode/helpers.lua replaces the global with a memory walk
    GetUserTeamID = function() return s.in_cm and (s.user_team or 0) or 0 end
    GetCurrentDate = function() return { day = s.date.day, month = s.date.month, year = s.date.year } end
    GetPlayerName = function(pid) return "Player " .. tostring(pid) end
    GetTeamName = function(tid) return s.team_names[tid] or ("Team " .. tostring(tid)) end
    GetTeamIdFromPlayerId = function(pid) return s.player_team and s.player_team[pid] or 0 end
    GetCompetitionNameByObjID = function(id) return "Comp " .. tostring(id) end
    GetCompetitionNameByID = function(id) return "CompId " .. tostring(id) end
    SetPlayerForm = function(pid, v) s:record("SetPlayerForm", pid, v) end
    SetPlayerMorale = function(pid, v) s:record("SetPlayerMorale", pid, v) end
    SetPlayerFitness = function(pid, v) s:record("SetPlayerFitness", pid, v) end
    GetPlayersStats = function() return s.stats end
    PlayerExists = function(pid) return s:find_row("players", "playerid", pid) ~= nil end
    DeletePlayer = function(pid, tid)
        s:record("DeletePlayer", pid, tid)
        local rec = s:find_row("players", "playerid", pid)
        if rec then
            local t = s.tables.players
            s:w8(rec + t.rec_size - 1, 0x80)
        end
    end
    TerminateLoan = function(pid) s:record("TerminateLoan", pid) end
    cTransferPlayer = function(...) s:record("cTransferPlayer", ...) end
    cLoanPlayer = function(...) s:record("cLoanPlayer", ...) end
    cReleasePlayer = function(...) s:record("cReleasePlayer", ...) end
    s.transfer_listed, s.loan_listed = {}, {}
    cIsPlayerTransferListed = function(pid) return s.transfer_listed[pid] == true end
    cIsPlayerLoanListed = function(pid) return s.loan_listed[pid] == true end
    cAddPlayerToTransferList = function(pid, ...) s:record("cAddPlayerToTransferList", pid, ...); s.transfer_listed[pid] = true end
    cAddPlayerToLoanList = function(pid, ...) s:record("cAddPlayerToLoanList", pid, ...); s.loan_listed[pid] = true end
    cRemovePlayerFromLists = function(pid, ...)
        s:record("cRemovePlayerFromLists", pid, ...)
        s.transfer_listed[pid], s.loan_listed[pid] = nil, nil
    end
    cRemovePlayerFromTransferList = function(...) s:record("cRemovePlayerFromTransferList", ...) end
    cRemovePlayerFromLoanList = function(...) s:record("cRemovePlayerFromLoanList", ...) end
    cGetTransferBans = function()
        local out = {}
        for _, b in ipairs(s.bans) do out[#out + 1] = { id = b.id, member_type = b.member_type, date_end = b.date_end } end
        return out
    end
    cAddTransferBan = function(id, date, typ)
        s:record("cAddTransferBan", id, date, typ)
        table.insert(s.bans, { id = id, member_type = typ, date_end = date })
    end
    cRemoveTransferBan = function(id, typ)
        s:record("cRemoveTransferBan", id, typ)
        for i = #s.bans, 1, -1 do
            if s.bans[i].id == id and s.bans[i].member_type == typ then table.remove(s.bans, i) end
        end
    end
    cSaveTransferBans = function() s:record("cSaveTransferBans") end
    PlayerDevelopmentManagerLoad = function() end
    PlayerDevelopmentManagerSave = function() s:record("PlayerDevelopmentManagerSave") end
    PlayerDevelopmentManagerAddPlayer = function(...) s:record("PlayerDevelopmentManagerAddPlayer", ...) end
    PlayerDevelopmentManagerRemovePlayer = function(...) s:record("PlayerDevelopmentManagerRemovePlayer", ...) end

    LE_VERSION = "v27.1.0"
    LE_GAME_MODULE_BASE = 0x140000000
    LE_GAME_MODULE_SIZE = 0x211EF000
    LE_GAME_MODULE_NAME = "FC27.exe"
end

function Sim:fire(ev, ...)
    for _, h in ipairs(self.handlers[ev] or {}) do h.fn(...) end
end

------------------------------------------------------------------ T3DB builder
-- spec = { name, short, fields = { {name, short, type="int"|"float"|"string", depth, min} }, rows = { {field=value} } }
function Sim:add_table(spec)
    local cols = {}
    local bit = 0
    for _, f in ipairs(spec.fields) do
        assert(#f.short == 4, "field shortname must be 4 chars: " .. f.name)
        -- "compressed": FC 27's compressed text (field type 13, e.g. playernames.name). In memory it is not plain text
        -- (the sim stores the bytes XOR 0xA5); only Live Editor's GetDBTableRows decodes it.
        local typ = (f.type == "string" and 0) or (f.type == "float" and 4) or (f.type == "compressed" and 13) or 3
        local depth = f.depth or (f.type == "float" and 32) or ((typ == 13) and 8 * 32) or 16
        if typ == 0 or typ == 13 then bit = (bit + 7) // 8 * 8 end
        cols[#cols + 1] = { name = f.name, short = f.short, typ = typ, depth = depth, min = f.min or 0, bitoff = bit }
        bit = bit + depth
    end
    local data_bytes = (bit + 7) // 8
    local rec_size = data_bytes + 1
    local n = #spec.rows
    local first = self:alloc(rec_size * math.max(n, 1) + 16, 8)

    local hdr = self:alloc(0x84 + #cols * 0x10, 8)
    self:w64(hdr + 0x30, first)
    for i = 1, 4 do self:w8(hdr + 0x40 + i - 1, spec.short:byte(i)) end
    self:w32(hdr + 0x44, rec_size)
    self:w16(hdr + 0x7C, n)
    self:w8(hdr + 0x82, #cols)
    self.meta.shortname_name_tables_map[spec.short] = spec.name
    self.meta.field_desc_map[spec.short] = {}
    for i, c in ipairs(cols) do
        local ca = hdr + 0x84 + (i - 1) * 0x10
        self:w32(ca, c.typ)
        self:w32(ca + 4, c.bitoff)
        for k = 1, 4 do self:w8(ca + 8 + k - 1, c.short:byte(k)) end
        self:w32(ca + 0xC, c.depth)
        local max = (c.typ == 3 and c.depth < 63) and (c.min + (1 << c.depth) - 1) or 0
        self.meta.field_desc_map[spec.short][c.short] = { name = c.name, depth = c.depth, min = c.min, max = max,
                                                           field_type = c.typ, shortname = c.short, table_name = spec.name }
    end

    local t = { name = spec.name, hdr = hdr, first = first, rec_size = rec_size, cols = cols, n = n }
    for _, c in ipairs(cols) do t[c.name] = c end
    self.tables[spec.name] = t
    for r, row in ipairs(spec.rows) do
        local rec = first + (r - 1) * rec_size
        for _, c in ipairs(cols) do
            local v = row[c.name]
            if v ~= nil then self:set_field(rec, c, v) end
        end
        self:w8(rec + rec_size - 1, row.__invalid and 0x80 or 0)
    end
    -- link into the DB chain
    self:link_table(hdr)
    return t
end

function Sim:set_field(rec, c, v)
    if c.typ == 13 then
        local a = rec + c.bitoff // 8
        local str = tostring(v)
        for i = 0, c.depth // 8 - 1 do
            local b = str:byte(i + 1)
            self:wb(a + i, b and (b ~ 0xA5) or 0xA5)
        end
        return
    end
    if c.typ == 0 then
        local a = rec + c.bitoff // 8
        for i = 0, c.depth // 8 - 1 do self:wb(a + i, 0) end
        self:wstr(a, v)
        return
    end
    local raw
    if c.typ == 4 then
        raw = string.unpack("<I4", string.pack("<f", v))
    else
        raw = v - c.min
        assert(raw >= 0 and raw < (1 << c.depth), string.format("value %s out of range for %s", tostring(v), c.name))
    end
    local byte = c.bitoff // 8
    local sb = c.bitoff % 8
    local addr = rec + byte
    local q = self:r64(addr)
    local mask = ((1 << c.depth) - 1) << sb
    q = (q & ~mask) | ((raw << sb) & mask)
    self:w64(addr, q)
end

function Sim:get_field(rec, c)
    if c.typ == 0 then return ReadString(rec + c.bitoff // 8) end
    if c.typ == 13 then
        local out = {}
        for i = 0, c.depth // 8 - 1 do
            local b = self:rb(rec + c.bitoff // 8 + i) ~ 0xA5
            if b == 0 then break end
            out[#out + 1] = string.char(b)
        end
        return table.concat(out)
    end
    local q = self:r64(rec + c.bitoff // 8) >> (c.bitoff % 8)
    local raw = q & ((1 << c.depth) - 1)
    if c.typ == 4 then return (string.unpack("<f", string.pack("<I4", raw))) end
    return raw + c.min
end

function Sim:rows(name)
    local t = self.tables[name]
    local out = {}
    if not t then return out end
    for i = 0, t.n - 1 do
        local rec = t.first + i * t.rec_size
        if (self:rb(rec + t.rec_size - 1) & 0x80) == 0 then out[#out + 1] = rec end
    end
    return out
end

function Sim:value(name, rec, field)
    return self:get_field(rec, self.tables[name][field])
end

function Sim:find_row(name, field, value)
    for _, rec in ipairs(self:rows(name)) do
        if self:value(name, rec, field) == value then return rec end
    end
    return nil
end

function Sim:ensure_db()
    if self.db_node then return end
    local svc = self:alloc(0x40, 8)
    local a = self:alloc(0x20, 8)
    local b = self:alloc(0x20, 8)
    local dbn = self:alloc(0x20, 8)
    self:w64(svc + 0x20, a)
    self:w64(a + 0x08, b)
    self:w64(b + 0x10, dbn)
    self:w64(dbn + 0x10, 0)
    self:w64(dbn + 0x18, 0)
    self.db_node = dbn
    self.last_table = nil
    self.plugins[0x0ae932d0] = svc + 8   -- ENUM_djb2Database_CLSS (t3db subtracts 8)
end

function Sim:link_table(hdr)
    self:ensure_db()
    if self.last_table then
        self:w64(self.last_table + 0x08, hdr)
    else
        self:w64(self.db_node + 0x10, hdr)
    end
    self.last_table = hdr
end

------------------------------------------------------------------ managers
-- Layout read by career_mode/helpers.lua GetManagerObjByTypeId
function Sim:ensure_comm()
    if self.mode_managers then return end
    local comm = self:alloc(0x40, 8)
    local x = self:alloc(0x40, 8)
    local mm = self:alloc(0x20 * 256, 8)
    self:w64(comm + 0x20, x)
    self:w64(x + 0x10, mm)
    self.mode_managers = mm
    self.plugins[0x1297f047] = comm   -- ENUM_djb2FeFceGMCommServiceInterface_CLSS
end

function Sim:add_manager(type_id, size)
    self:ensure_comm()
    local obj = self:alloc(size or 0x4000, 16)
    local entry = self.mode_managers + 0x20 * type_id
    local typ = self:alloc(0x20, 8)
    local holder = self:alloc(0x10, 8)
    self:w32(entry + 0x10, 1)
    self:w64(entry + 0x8, typ)
    self:w32(typ + 0x10, 1)
    self:w64(entry + 0x18, holder)
    self:w64(holder, obj)
    self.managers[type_id] = obj
    return obj
end

-- eastl::vector {begin, end} at addr with `count` entries of `size` bytes; returns begin
function Sim:make_vector(addr, count, size)
    if count == 0 then
        self:w64(addr, 0)
        self:w64(addr + 8, 0)
        return 0
    end
    local b = self:alloc(count * size + 16, 8)
    self:w64(addr, b)
    self:w64(addr + 8, b + count * size)
    return b
end

function Sim:db_meta()
    if self.meta_mode == "table" then return self.meta end
    local iterable = self.meta_mode == "userdata"
    local fmap = {}
    for tshort, fm in pairs(self.meta.field_desc_map) do
        local descs = {}
        for fshort, d in pairs(fm) do descs[fshort] = proxy(d, false) end
        fmap[tshort] = proxy(descs, iterable)
    end
    return proxy({
        shortname_name_tables_map = proxy(self.meta.shortname_name_tables_map, iterable),
        field_desc_map = proxy(fmap, iterable),
    }, false)
end

return Sim
