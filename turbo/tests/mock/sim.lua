-- Simulated Live Editor runtime for offline tests.
-- Byte-addressable memory with page mapping, the native functions FC 27 LE exposes to Lua,
-- and builders for T3DB tables and FCE career-mode managers laid out exactly as the real
-- Live Editor Lua libraries (t3db/*.lua, career_mode/helpers.lua) expect.

local Sim = {}
Sim.__index = Sim

local PAGE = 12

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
    s.tables = {}
    s.managers = {}
    s.bans = {}
    s.stats = {}
    s.team_names = {}
    s.date = { day = 15, month = 1, year = 2027 }
    return s
end

------------------------------------------------------------------ memory
function Sim:alloc(size, align)
    align = align or 16
    local a = (self.next_alloc + align - 1) // align * align
    self.next_alloc = a + size + 0x40
    for p = a >> PAGE, (a + size + 16) >> PAGE do self.pages[p] = true end
    return a
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
    MessageBox = function(title, text) table.insert(s.boxes, { title = title, text = text }) end
    IsInCM = function() return s.in_cm end
    GetPlugin = function(hash) return s.plugins[hash] or 0 end
    GetDBMeta = function() return s.meta end
    GetSaveUID = function() return "TESTSAVE" end

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
    cIsPlayerTransferListed = function() return false end
    cIsPlayerLoanListed = function() return false end
    cAddPlayerToTransferList = function(...) s:record("cAddPlayerToTransferList", ...) end
    cAddPlayerToLoanList = function(...) s:record("cAddPlayerToLoanList", ...) end
    cRemovePlayerFromLists = function(...) s:record("cRemovePlayerFromLists", ...) end
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
        local typ = (f.type == "string" and 0) or (f.type == "float" and 4) or 3
        local depth = f.depth or (f.type == "float" and 32) or 16
        if typ == 0 then bit = (bit + 7) // 8 * 8 end
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
        self.meta.field_desc_map[spec.short][c.short] = { name = c.name, depth = c.depth, min = c.min }
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

return Sim
