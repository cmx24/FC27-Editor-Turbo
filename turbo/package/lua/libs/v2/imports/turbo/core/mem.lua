-- FC 27 LE Turbo - defensive memory helpers
-- Live Editor's memory natives crash the game on an unreadable address (FC 27, 02-10-2026: a read of 0x3600000028 while
-- Turbo searched career-mode memory). Every read here is first checked against the readable-memory map Turbo.dll
-- publishes (turbogui/src/core/memmap.h): without the Turbo GUI running there is no map and nothing is read.

local M = {}

M.MIN_PTR = 0x10000
M.MAX_PTR = 0x7FFFFFFEFFFF
M.NO_MAP = "the Turbo GUI is not running in this game session (it tells Turbo which game memory is safe to read); " ..
    "press F8 in game or run turbo_gui_load.lua, then try again"

local MAP_MAGIC, MAP_HEADER, MAILBOX_MAP_PTR = 0x4D4D5254, 0x20, 0x18
local map_cache = { addr = nil, checked = -1, seq = -1, s = 0, e = 0 }

function M.available()
    return type(MEMORY) == "table" and type(MEMORY.ReadPointer) == "function"
end

local function clock()
    return (type(os) == "table" and type(os.clock) == "function") and os.clock() or 0
end

-- Address of Turbo.dll's map (via the GUI mailbox), cached for two seconds; nil without the GUI
function M.map_address()
    local now = clock()
    if map_cache.addr and now - map_cache.checked < 2 then return map_cache.addr end
    map_cache.checked = now
    map_cache.addr = nil
    if not M.available() then return nil end
    local okb, bridge = pcall(require, 'imports/turbo/bridge')
    if not okb or type(bridge.mailbox_address) ~= "function" then return nil end
    local okm, mb = pcall(bridge.mailbox_address)
    if not okm or not mb then return nil end
    local map = MEMORY:ReadPointer(mb + MAILBOX_MAP_PTR)
    if math.type(map) ~= "integer" or map < M.MIN_PTR or map > M.MAX_PTR then return nil end
    if MEMORY:ReadInt(map) ~= MAP_MAGIC then return nil end
    map_cache.addr = map
    return map
end

function M.map_available() return M.map_address() ~= nil end

-- true when [addr, addr + len) lies inside one readable region of the game process
function M.readable(addr, len)
    if math.type(addr) ~= "integer" or addr < M.MIN_PTR or addr > M.MAX_PTR then return false end
    len = len or 8
    local map = M.map_address()
    if not map then return false end
    for _ = 1, 3 do
        local s1 = MEMORY:ReadInt(map + 8)
        if s1 % 2 == 0 then
            if s1 == map_cache.seq and addr >= map_cache.s and addr + len <= map_cache.e then return true end
            local count = MEMORY:ReadInt(map + 0x0C)
            local cap = MEMORY:ReadInt(map + 0x10)
            if count >= 0 and count <= cap then
                local lo, hi = 0, count   -- first entry whose end is above addr
                while lo < hi do
                    local mid = (lo + hi) // 2
                    if MEMORY:ReadQword(map + MAP_HEADER + mid * 16 + 8) > addr then hi = mid else lo = mid + 1 end
                end
                local found, rs, re = false, 0, 0
                if lo < count then
                    rs = MEMORY:ReadQword(map + MAP_HEADER + lo * 16)
                    re = MEMORY:ReadQword(map + MAP_HEADER + lo * 16 + 8)
                    found = rs <= addr and addr + len <= re
                end
                if MEMORY:ReadInt(map + 8) == s1 then
                    if found then map_cache.seq, map_cache.s, map_cache.e = s1, rs, re end
                    return found
                end
            end
        end
    end
    return false
end

-- align: required alignment (default 4)
function M.is_ptr(p, align)
    if math.type(p) ~= "integer" then return false end
    if p < M.MIN_PTR or p > M.MAX_PTR then return false end
    align = align or 4
    return p % align == 0
end

-- Read a pointer stored at addr. Returns pointer or nil.
function M.ptr(addr, align)
    if not M.readable(addr, 8) then return nil end
    local v = MEMORY:ReadPointer(addr)
    if M.is_ptr(v, align or 8) then return v end
    return nil
end

-- Follow base -> [+off1] -> [+off2] ...; every hop must be a valid pointer.
function M.chain(base, offsets)
    if not M.is_ptr(base, 1) then return nil end
    local cur = base
    for i = 1, #offsets do
        cur = M.ptr(cur + offsets[i])
        if not cur then return nil end
    end
    return cur
end

function M.int(addr)
    if not M.readable(addr, 4) then return nil end
    return MEMORY:ReadInt(addr)
end

function M.short(addr)
    if not M.readable(addr, 2) then return nil end
    return MEMORY:ReadShort(addr)
end

function M.byte(addr)
    if not M.readable(addr, 1) then return nil end
    local b = MEMORY:ReadBytes(addr, 1)
    return b and b[1] or nil
end

function M.bool(addr)
    local b = M.byte(addr)
    if b == nil then return nil end
    return b ~= 0
end

-- eastl::vector style {begin, end} stored at addr.
-- Returns begin, end, count or nil, reason
function M.vector(addr, elem_size, max_count)
    if not M.readable(addr, 16) then return nil, "vector address is not readable" end
    local b = M.ptr(addr, 4)
    local e = M.ptr(addr + 8, 4)
    if not b or not e then
        -- Empty vectors are commonly {0, 0}
        if MEMORY:ReadPointer(addr) == 0 and MEMORY:ReadPointer(addr + 8) == 0 then  -- readable: checked above
            return 0, 0, 0
        end
        return nil, "begin/end are not pointers"
    end
    if e < b then return nil, "end < begin" end
    local span = e - b
    if span % elem_size ~= 0 then return nil, "size not a multiple of element size" end
    local count = span // elem_size
    if max_count and count > max_count then return nil, "too many elements" end
    if span > 0 and not M.readable(b, span) then return nil, "elements are not readable" end
    return b, e, count
end

-- The career manager table ("ctx"): managers = [[comm+0x20]+0x10] of the FeFceGMCommService plugin, the table Live
-- Editor's GetManagerObjByTypeId walks (career_mode/helpers.lua). nil when the plugin or a hop is missing.
function M.manager_table()
    pcall(require, 'imports/services/enums')
    local id = _G["ENUM_djb2FeFceGMCommServiceInterface_CLSS"]
    if type(GetPlugin) ~= "function" or type(id) ~= "number" then return nil end
    local ok, comm = pcall(GetPlugin, id)
    if not ok or math.type(comm) ~= "integer" or comm <= 0 then return nil end
    return M.chain(comm, { 0x20, 0x10 })
end

-- Manager object by FCE type id: the walk of Live Editor's GetManagerObjByTypeId (career_mode/helpers.lua), with every
-- read checked (Live Editor's own version reads unchecked and crashes outside a career)
function M.manager(type_id)
    if math.type(type_id) ~= "integer" and type(type_id) ~= "number" then return nil end
    local managers = M.manager_table()
    if not managers then return nil end
    local slot = managers + 0x20 * math.tointeger(type_id)
    if M.int(slot + 0x10) ~= 1 then return nil end
    local mtype = M.ptr(slot + 0x8)
    if not mtype or M.int(mtype + 0x10) ~= 1 then return nil end
    local addr = M.chain(slot, { 0x18, 0x0 })
    if addr and M.is_ptr(addr, 8) then return addr end
    return nil
end

return M
