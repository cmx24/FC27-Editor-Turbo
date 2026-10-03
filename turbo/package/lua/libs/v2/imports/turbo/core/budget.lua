-- FC 27 LE Turbo - your club's transfer budget, read and written by Turbo itself (FC 27 LE v27.1.2 has no
-- Get/SetUserTransferBudget). Found in game (03-10-2026, test career "turbolab", Juventus, budget $81,497,280 in
-- Office > Finances): the user club's finance entry E is reached from the career UserManager:
--   user_info = UserManager(type 129) + 0x18          (FC 27 LE's own helpers read the club id at user_info + 0x1F4)
--   E         = *( *(user_info + 0x2F8) + 0x08 )
--   E + 0x28  = club id (must be your club)
--   E - 0x10  = transfer budget, E + 0x08 = the same value again (both hold the budget the Finances screen shows)
-- Writing both made the game show the new budget (Office > Budget Overview $81M -> $91M). Values are in the currency the
-- game shows. Every read and write goes through the readable-memory map (core/mem.lua).

local mem = require 'imports/turbo/core/mem'

local M = {}

M.USER_MANAGER = 129
M.USER_INFO = 0x18
M.USER_TEAM = 0x1F4
M.FINANCE_LIST = 0x2F8
M.ENTRY_SLOTS = { 0x08, 0x00, 0x10, 0x18 }   -- slot 0x08 seen in game; the others are checked too
M.ENTRY_TEAM = 0x28
M.BUDGET_A = -0x10
M.BUDGET_B = 0x08
M.MAX = 2000000000

-- Returns { a = addr, b = addr|nil, team = id } or nil, reason
function M.locate()
    if not mem.map_available() then return nil, mem.NO_MAP end
    local um = mem.manager(M.USER_MANAGER)
    if not um then return nil, "career UserManager not found (is a career loaded?)" end
    local ui = mem.ptr(um + M.USER_INFO)
    if not ui then return nil, "career user info not readable" end
    local team = mem.int(ui + M.USER_TEAM)
    if not team or team <= 0 then return nil, "your club id was not found" end
    local list = mem.ptr(ui + M.FINANCE_LIST)
    if not list then return nil, "your club's finance list was not found" end
    for _, slot in ipairs(M.ENTRY_SLOTS) do
        local e = mem.ptr(list + slot)
        if e and mem.readable(e + M.BUDGET_A, 0x40) and mem.int(e + M.ENTRY_TEAM) == team then
            local a = e + M.BUDGET_A
            local va = mem.int(a)
            if va and va >= 0 and va <= M.MAX then
                local b = e + M.BUDGET_B
                if mem.int(b) ~= va then b = nil end   -- only mirror the second copy when it holds the same value
                return { a = a, b = b, team = team }
            end
        end
    end
    return nil, string.format("no finance entry for club %d was found", team)
end

function M.get()
    local loc, err = M.locate()
    if not loc then return nil, err end
    return mem.int(loc.a)
end

-- Returns true, new_value or false, error
function M.set(value)
    if math.type(value) ~= "integer" or value < 0 or value > M.MAX then
        return false, string.format("budget must be 0..%d", M.MAX)
    end
    local loc, err = M.locate()
    if not loc then return false, err end
    local ok, werr = pcall(function()
        MEMORY:WriteInt(loc.a, value)
        if loc.b then MEMORY:WriteInt(loc.b, value) end
    end)
    if not ok then return false, "writing the budget failed: " .. tostring(werr) end
    local now = mem.int(loc.a)
    if now ~= value then return false, string.format("budget reads %s after writing %d", tostring(now), value) end
    return true, now
end

return M
