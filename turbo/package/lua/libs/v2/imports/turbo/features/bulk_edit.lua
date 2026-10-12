-- Turbo feature: bulk edit players (FC 26 LE v26.3.2 / v26.3.5 "Bulk Edit Players").
--   "bulk_edit": {
--     "scope":   { "user_team": true },             -- or teamids / playerids / all (+ "confirm_all": true)
--     "filters": { "max_age": 21, "min_potential": 80 },
--     "set":     { "potential": 90, "isretiring": 0 },  -- any players-table fields, range-checked
--     "actions": { "fitness": 95, "form": 100, "morale": 100,
--                  "development": { "xp_multiplier": 2.0, "bonus_xp": 0, "no_decline": true } }
--   }
-- Every field and value is validated before the first write; one bad entry stops the whole run.
-- In a career, "set" fields the game's development plan holds (attributes, work rates, skill moves, weak foot:
-- core/moves.lua PLAN_FIELDS) go into the plan of each of your players too, else the plan puts the old values back
-- when the career is loaded again.
-- 2.1: "set" attributes / preferredposition1 make the stored overall follow (core/overall.lua: the Turbo window's
-- formula and rule), so the game shows the new overall at once. Not when "set" has overallrating itself, with
-- "overall_follow": false, or while turbo_output\overall_follow_off.txt exists. The overalls it wrote are kept in
-- turbo_output\overall_follow_undo.json (the last runs); { "undo_overall": true } puts the last run's back (only where
-- the overall is still the one it wrote).

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local moves = require 'imports/turbo/core/moves'
local sel = require 'imports/turbo/core/select'
local overall = require 'imports/turbo/core/overall'

local M = {}

local ACTION_RANGES = {
    form = { 0, 100, "SetPlayerForm" },
    morale = { 0, 100, "SetPlayerMorale" },
    fitness = { 5, 95, "SetPlayerFitness" },
}

local function plan_actions(actions)
    local steps = {}
    actions = type(actions) == "table" and actions or {}
    for key, v in pairs(actions) do
        if key == "development" then
            if type(v) ~= "table" then return nil, "actions.development must be an object" end
            local mult = tonumber(v.xp_multiplier or 1.0)
            local bonus = util.to_int(v.bonus_xp or 0)
            local no_decline = v.no_decline == true
            if not mult or mult < 0 or mult > 100 then return nil, "development.xp_multiplier must be 0..100" end
            if not bonus or bonus < 0 then return nil, "development.bonus_xp must be an integer >= 0" end
            if type(PlayerDevelopmentManagerAddPlayer) ~= "function" or type(PlayerDevelopmentManagerSave) ~= "function" then
                return nil, "player development natives are not available in this Live Editor build"
            end
            steps[#steps + 1] = { kind = "development", mult = mult + 0.0, bonus = bonus, no_decline = no_decline }
        elseif ACTION_RANGES[key] then
            local r = ACTION_RANGES[key]
            local iv = util.to_int(v)
            if iv == nil or iv < r[1] or iv > r[2] then return nil, string.format("actions.%s must be %d..%d", key, r[1], r[2]) end
            if type(_G[r[3]]) ~= "function" then return nil, r[3] .. " is not available in this Live Editor build" end
            steps[#steps + 1] = { kind = "api", fn = r[3], value = iv, key = key }
        else
            return nil, "unknown action: " .. tostring(key)
        end
    end
    table.sort(steps, function(a, b) return (a.key or a.kind) < (b.key or b.kind) end)
    return steps
end

M.UNDO_FILE = "overall_follow_undo.json"
M.UNDO_FORMAT = "turbo_overall_follow_undo"
M.UNDO_RUNS = 20   -- runs kept in the file

local function json_lib()
    local ok, j = pcall(require, 'imports/external/json')
    return ok and type(j) == "table" and j or nil
end

local function read_undo(out_dir)
    local j = json_lib()
    local path = util.join(out_dir, M.UNDO_FILE)
    if not j then return nil, path, "json library not available" end
    if not util.file_exists(path) then return { format = M.UNDO_FORMAT, runs = {} }, path end
    local ok, d = pcall(j.decode, util.read_file(path) or "")
    if not ok or type(d) ~= "table" or d.format ~= M.UNDO_FORMAT or type(d.runs) ~= "table" then
        return nil, path, "undo file " .. path .. " is not readable (left as it is)"
    end
    return d, path
end

local function write_undo(path, doc)
    local j = json_lib()
    local ok, text = pcall(j.encode, doc)
    if not ok then return false, tostring(text) end
    return util.write_file(path, text)
end

-- Puts the last run's overalls back where the overall is still the one the run wrote
local function undo_overall(ctx)
    local players, perr = db.get_table("players")
    if not players then return false, perr end
    local doc, path, err = read_undo(ctx.out_dir)
    if not doc then return false, err end
    local run = doc.runs[#doc.runs]
    if not run or type(run.players) ~= "table" then return true, "no overall follow to undo" end
    local want = {}
    for _, e in ipairs(run.players) do want[util.to_int(e.playerid) or -1] = e end
    local back, changed = 0, 0
    for rec in db.records(players) do
        local e = want[db.get_int(players, rec, "playerid", -1)]
        if e then
            if db.get_int(players, rec, "overallrating", 0) == util.to_int(e.after) then
                local ok, werr = db.set(players, rec, "overallrating", util.to_int(e.before), ctx.dry)
                if not ok then return false, string.format("player %s: %s", tostring(e.playerid), werr) end
                back = back + 1
            else
                changed = changed + 1
            end
        end
    end
    if ctx.dry then return true, string.format("DRY RUN: %d overalls would go back", back) end
    doc.runs[#doc.runs] = nil
    local ok, werr = write_undo(path, doc)
    local msg = string.format("%d overalls put back (bulk edit of %s)", back, tostring(run.when))
    if changed > 0 then msg = msg .. string.format("; %d changed since, left as they are", changed) end
    if not ok then msg = msg .. "; undo file not updated: " .. tostring(werr) end
    return true, msg
end

function M.run(ctx)
    local cfg = ctx.cfg
    if cfg.undo_overall == true then return undo_overall(ctx) end
    local list, players_or_err, desc = sel.players(cfg.scope, cfg.filters, { allow_all = cfg.confirm_all == true })
    if not list then return false, players_or_err end
    local players = players_or_err

    -- Validate field writes
    local writes = {}
    for field, value in pairs(type(cfg.set) == "table" and cfg.set or {}) do
        if field == "playerid" then return false, "playerid cannot be bulk-edited" end
        local v, verr = db.validate(players, field, value)
        if v == nil then return false, verr end
        writes[#writes + 1] = { field = field, value = v }
    end
    table.sort(writes, function(a, b) return a.field < b.field end)

    local steps, serr = plan_actions(cfg.actions)
    if not steps then return false, serr end
    if #writes == 0 and #steps == 0 then return false, "nothing to do: \"set\" and \"actions\" are empty" end
    if #steps > 0 and not game.in_cm() then return false, "actions (fitness/form/morale/development) need a loaded career" end
    if #list == 0 then return true, "no players matched " .. tostring(desc) end

    local is_plan, plan_vals = util.set_of(moves.PLAN_FIELDS), {}
    for _, w in ipairs(writes) do
        if is_plan[w.field] then plan_vals[w.field] = w.value end
    end
    local sync_plans = next(plan_vals) ~= nil and game.in_cm()
    -- the overall follows the attributes / position (core/overall.lua)
    local follow, follow_note = false, nil
    for _, w in ipairs(writes) do
        if overall.follows_field(w.field) then follow = true end
    end
    for _, w in ipairs(writes) do
        if w.field == "overallrating" then follow = false end
    end
    if follow and cfg.overall_follow == false then follow = false end
    if follow and overall.off(ctx.out_dir) then
        follow, follow_note = false, "overall not followed (turbo_output\\" .. overall.KILL_FILE .. ")"
    end
    local followed = {}
    local api_fail, dev_added, plans, plan_fail = 0, 0, 0, {}
    for _, p in ipairs(list) do
        local a0, pos0, ovr0
        if follow then
            a0, pos0 = overall.read(db, players, p.rec)
            ovr0 = a0 and db.get_int(players, p.rec, "overallrating", 0)
        end
        for _, w in ipairs(writes) do
            local ok, werr = db.set(players, p.rec, w.field, w.value, ctx.dry)
            if not ok then return false, string.format("player %d: %s", p.pid, werr) end
        end
        if a0 then
            local a1, pos1 = {}, pos0
            for k, v in pairs(a0) do a1[k] = v end
            for _, w in ipairs(writes) do
                if w.field == "preferredposition1" then pos1 = w.value elseif a1[w.field] ~= nil then a1[w.field] = w.value end
            end
            local want = overall.follow(pos0, a0, pos1, a1, ovr0)
            if want ~= ovr0 then
                local ok, werr = db.set(players, p.rec, "overallrating", want, ctx.dry)
                if not ok then return false, string.format("player %d: overall %d -> %d: %s", p.pid, ovr0, want, werr) end
                followed[#followed + 1] = { playerid = p.pid, before = ovr0, after = want }
            end
        end
        if sync_plans and not ctx.dry then
            local state = moves.sync_plan(p.pid, plan_vals)
            if state == "plan" then plans = plans + 1
            elseif state:sub(1, 7) == "failed:" then plan_fail[#plan_fail + 1] = string.format("%d (%s)", p.pid, state:sub(9)) end
        end
        if not ctx.dry then
            for _, s in ipairs(steps) do
                if s.kind == "api" then
                    if not pcall(_G[s.fn], p.pid, s.value) then api_fail = api_fail + 1 end
                elseif s.kind == "development" then
                    if pcall(PlayerDevelopmentManagerAddPlayer, p.pid, s.mult, s.bonus, s.no_decline) then
                        dev_added = dev_added + 1
                    else
                        api_fail = api_fail + 1
                    end
                end
            end
        end
    end
    if dev_added > 0 then pcall(PlayerDevelopmentManagerSave) end

    local parts = {}
    for _, w in ipairs(writes) do parts[#parts + 1] = w.field .. "=" .. tostring(w.value) end
    for _, s in ipairs(steps) do
        if s.kind == "api" then parts[#parts + 1] = s.key .. "=" .. s.value
        else parts[#parts + 1] = string.format("development(x%.2f,+%d,%s)", s.mult, s.bonus, s.no_decline and "no decline" or "decline") end
    end
    local summary = string.format("%d players (%s): %s", #list, desc, table.concat(parts, ", "))
    if plans > 0 then summary = summary .. string.format("; %d development plans updated", plans) end
    if #followed > 0 then
        summary = summary .. string.format("; overall followed the attributes for %d players", #followed)
        if not ctx.dry then
            local doc, path, uerr = read_undo(ctx.out_dir)
            if doc then
                doc.runs[#doc.runs + 1] = { when = os.date("%Y-%m-%d %H:%M:%S"), players = followed }
                while #doc.runs > M.UNDO_RUNS do table.remove(doc.runs, 1) end
                local ok, werr = write_undo(path, doc)
                if not ok then uerr = "undo file not written: " .. tostring(werr) end
            end
            summary = summary .. (uerr and ("; " .. uerr) or " (undo: overall_follow_undo.json)")
        end
    end
    if follow_note then summary = summary .. "; " .. follow_note end
    if api_fail > 0 then summary = summary .. string.format("; %d API calls failed", api_fail) end
    if #plan_fail > 0 then summary = summary .. "; development plan NOT updated for " .. table.concat(plan_fail, ", ") end
    return api_fail == 0 and #plan_fail == 0, summary
end

return M
