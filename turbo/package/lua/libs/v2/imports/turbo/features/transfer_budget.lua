-- Turbo feature: your club's transfer budget (career mode).
-- FC 27's teams table has no transferbudget field any more. When Live Editor has the natives GetUserTransferBudget /
-- SetUserTransferBudget they are used; FC 27 LE v27.1.2 does not ship them, so Turbo then reads and writes the budget
-- in the career's own memory itself (core/budget.lua). DOC.MD still lists GetTransferBudget / SetTransferBudget, but
-- FC 27 Live Editor's lua\libs\v1\live_editor.lua turned those into deprecation stubs that do nothing, so Turbo never
-- calls them.
--   "transfer_budget": { "mode": "get" | "set" | "add", "amount": 50000000 }

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local budget = require 'imports/turbo/core/budget'

local M = {}

M.MAX = 2000000000   -- a 32-bit budget; Live Editor's int

local function current()
    local get = env.api("GetUserTransferBudget")
    if not get then return budget.get() end
    local ok, v = pcall(get)
    if not ok then return nil, "GetUserTransferBudget failed: " .. tostring(v) end
    v = util.to_int(v)
    if not v then return nil, "GetUserTransferBudget returned no number" end
    return v
end

M.current = current

function M.run(ctx)
    if not game.in_cm() then return false, "the transfer budget needs a loaded career" end
    local mode = ctx.cfg.mode or "get"
    local before, err = current()
    if not before then return false, err end
    if mode == "get" then return true, string.format("transfer budget: %d", before) end
    local amount = util.to_int(ctx.cfg.amount)
    if not amount then return false, "amount must be a whole number" end
    local target
    if mode == "set" then target = amount
    elseif mode == "add" then target = before + amount
    else return false, "mode must be get, set or add" end
    if target < 0 or target > M.MAX then
        return false, string.format("budget %d is outside 0..%d", target, M.MAX)
    end
    if ctx.dry then return true, string.format("dry run: budget would change %d -> %d", before, target) end
    local set = env.api("SetUserTransferBudget")
    if set then
        local ok, serr = pcall(set, target)
        if not ok then return false, "SetUserTransferBudget failed: " .. tostring(serr) end
    else
        local ok, serr = budget.set(target)
        if not ok then return false, serr end
    end
    local after = current()
    if after ~= target then
        return false, string.format("budget is %s after setting %d (the game did not take it)", tostring(after), target)
    end
    return true, string.format("transfer budget %d -> %d", before, after)
end

return M
