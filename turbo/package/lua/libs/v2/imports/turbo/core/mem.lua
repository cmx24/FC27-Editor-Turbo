-- FC 27 LE Turbo - defensive memory helpers
-- Only values that look like user-mode heap pointers are ever dereferenced.

local M = {}

M.MIN_PTR = 0x10000
M.MAX_PTR = 0x7FFFFFFEFFFF

function M.available()
    return type(MEMORY) == "table" and type(MEMORY.ReadPointer) == "function"
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
    if not M.is_ptr(addr, 1) then return nil end
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
    if not M.is_ptr(addr, 1) then return nil end
    return MEMORY:ReadInt(addr)
end

function M.short(addr)
    if not M.is_ptr(addr, 1) then return nil end
    return MEMORY:ReadShort(addr)
end

function M.byte(addr)
    if not M.is_ptr(addr, 1) then return nil end
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
    if not M.is_ptr(addr, 1) then return nil, "vector address is not a pointer" end
    local b = M.ptr(addr, 4)
    local e = M.ptr(addr + 8, 4)
    if not b or not e then
        -- Empty vectors are commonly {0, 0}
        if MEMORY:ReadPointer(addr) == 0 and MEMORY:ReadPointer(addr + 8) == 0 then
            return 0, 0, 0
        end
        return nil, "begin/end are not pointers"
    end
    if e < b then return nil, "end < begin" end
    local span = e - b
    if span % elem_size ~= 0 then return nil, "size not a multiple of element size" end
    local count = span // elem_size
    if max_count and count > max_count then return nil, "too many elements" end
    return b, e, count
end

-- Manager object by FCE type id, validated (wraps GetManagerObjByTypeId from career_mode/helpers)
function M.manager(type_id)
    if type(type_id) ~= "number" then return nil end
    local ok = pcall(require, 'imports/career_mode/helpers')
    if not ok or type(GetManagerObjByTypeId) ~= "function" then return nil end
    local ok2, addr = pcall(GetManagerObjByTypeId, type_id)
    if ok2 and M.is_ptr(addr, 8) then return addr end
    return nil
end

return M
