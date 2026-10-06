-- Turbo feature: edit rows of any database table that match conditions.
-- Covers FC 26 editor fields that live in the database (team name/budget fields, formations,
-- team and manager traits, staff tables) once the probe or export_table shows their names.
--   "db_edit": { "edits": [
--     { "table": "teams", "where": { "teamid": 1 }, "set": { "teamname": "Arsenal FC" } },
--     { "table": "formations", "where": { "teamid": 1 }, "set": { "offset1x": 0.5 } },
--     { "table": "players", "where": {}, "set": { "isretiring": 0 }, "allow_all_rows": true }
--   ] }
-- Each edit is validated (table, fields, value ranges) before any edit runs.
-- In a career, a players edit of a field the game's development plan holds (attributes, work rates, skill moves, weak
-- foot: core/moves.lua PLAN_FIELDS) goes into the plan of each of your players it changed, else the plan puts the old
-- value back when the career is loaded again.

local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'
local util = require 'imports/turbo/core/util'

local M = {}

local function prepare(e, i)
    if type(e) ~= "table" then return nil, string.format("edit %d is not an object", i) end
    if type(e.table) ~= "string" or e.table == "" then return nil, string.format("edit %d: table is missing", i) end
    local tbl, err = db.get_table(e.table)
    if not tbl then return nil, string.format("edit %d: %s", i, err) end

    local where = {}
    for field, value in pairs(type(e.where) == "table" and e.where or {}) do
        local v, verr = db.validate(tbl, field, value)
        if v == nil then return nil, string.format("edit %d where: %s", i, verr) end
        where[#where + 1] = { field = field, value = v }
    end
    if #where == 0 and e.allow_all_rows ~= true then
        return nil, string.format("edit %d: empty \"where\" changes every row; add \"allow_all_rows\": true to confirm", i)
    end

    local set = {}
    for field, value in pairs(type(e.set) == "table" and e.set or {}) do
        local v, verr = db.validate(tbl, field, value)
        if v == nil then return nil, string.format("edit %d set: %s", i, verr) end
        set[#set + 1] = { field = field, value = v }
    end
    if #set == 0 then return nil, string.format("edit %d: \"set\" is empty", i) end
    return { tbl = tbl, name = e.table, where = where, set = set }
end

local function matches(tbl, rec, where)
    for _, w in ipairs(where) do
        local cur = tbl:GetRecordFieldValue(rec, w.field)
        if math.type(w.value) == "float" or math.type(cur) == "float" then
            if math.abs((cur or 0) - w.value) > 1e-4 then return false end
        elseif cur ~= w.value then
            return false
        end
    end
    return true
end

function M.run(ctx)
    local edits = ctx.cfg.edits
    if type(edits) ~= "table" or #edits == 0 then return false, "edits list is empty" end
    local plan = {}
    for i, e in ipairs(edits) do
        local p, err = prepare(e, i)
        if not p then return false, err end
        plan[#plan + 1] = p
    end

    local parts, plans, plan_fail = {}, 0, {}
    local is_plan = util.set_of(moves.PLAN_FIELDS)
    for _, p in ipairs(plan) do
        local n = 0
        local recs = {}
        for rec in db.records(p.tbl) do
            if matches(p.tbl, rec, p.where) then recs[#recs + 1] = rec end
        end
        local plan_vals = {}
        if p.name == "players" and game.in_cm() then
            for _, s in ipairs(p.set) do if is_plan[s.field] then plan_vals[s.field] = s.value end end
        end
        for _, rec in ipairs(recs) do
            for _, s in ipairs(p.set) do
                local ok, werr = db.set(p.tbl, rec, s.field, s.value, ctx.dry)
                if not ok then return false, string.format("%s: %s", p.name, werr) end
            end
            if next(plan_vals) and not ctx.dry then
                local pid = p.tbl:GetRecordFieldValue(rec, "playerid")
                local state = moves.sync_plan(pid, plan_vals)
                if state == "plan" then plans = plans + 1
                elseif state:sub(1, 7) == "failed:" then plan_fail[#plan_fail + 1] = string.format("%s (%s)", tostring(pid), state:sub(9)) end
            end
            n = n + 1
        end
        parts[#parts + 1] = string.format("%s: %d rows", p.name, n)
    end
    local msg = table.concat(parts, "; ") .. string.format(" (%d edits)", util.count(plan))
    if plans > 0 then msg = msg .. string.format("; %d development plans updated", plans) end
    if #plan_fail > 0 then return false, msg .. "; development plan NOT updated for " .. table.concat(plan_fail, ", ") end
    return true, msg
end

return M
