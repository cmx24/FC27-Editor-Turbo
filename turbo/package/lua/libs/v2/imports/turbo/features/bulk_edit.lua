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
-- "overall_follow": false, or while turbo_output\overall_follow_off.txt exists. The overalls it wrote are recorded (also
-- when the run stops on a refused write) per run with the database it wrote (career or squads, club, load);
-- { "undo_overall": true } puts the last run's back, only in that database and only where the overall is still the one
-- it wrote. Another load of the same club's career (after a restart, a reload) needs "any_load": true as well.

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

-- ---------------------------------------------------------------- the overall-follow undo (2.1)
-- turbo_output\overall_follow_undo.json is a small index: one entry per run with when it ran and which database it
-- wrote (in_cm, the user's club, the bridge's Lua session + load generation). The overalls of each run sit in their own
-- text file, overall_follow_undo_<id>.txt ("playerid before after" lines), so a bulk edit or an undo never decodes the
-- other runs. Kept: the last UNDO_RUNS runs and at most UNDO_MAX_ENTRIES overalls in all (the newest run always).
M.UNDO_FILE = "overall_follow_undo.json"
M.UNDO_FORMAT = "turbo_overall_follow_undo/2"
M.UNDO_DATA = "overall_follow_undo_%d.txt"
M.UNDO_RUNS = 20
M.UNDO_MAX_ENTRIES = 50000

local function json_lib()
    local ok, j = pcall(require, 'imports/external/json')
    return ok and type(j) == "table" and j or nil
end

local function read_index(out_dir)
    local j = json_lib()
    local path = util.join(out_dir, M.UNDO_FILE)
    if not j then return nil, path, "json library not available" end
    if not util.file_exists(path) then return { format = M.UNDO_FORMAT, next_id = 1, runs = {} }, path end
    local ok, d = pcall(j.decode, util.read_file(path) or "")
    if not ok or type(d) ~= "table" or d.format ~= M.UNDO_FORMAT or type(d.runs) ~= "table" then
        return nil, path, "undo file " .. path .. " is not readable (left as it is)"
    end
    return d, path
end

local function write_index(path, doc)
    local j = json_lib()
    local ok, text = pcall(j.encode, doc)
    if not ok then return false, tostring(text) end
    return util.write_file(path, text)
end

local function data_path(out_dir, id)
    return util.join(out_dir, string.format(M.UNDO_DATA, util.to_int(id) or -1))
end

local function write_data(path, id, entries)
    local out = { string.format("#turbo-overall-undo %d %d", id, #entries) }
    for _, e in ipairs(entries) do out[#out + 1] = string.format("%d %d %d", e[1], e[2], e[3]) end
    return util.write_file(path, table.concat(out, "\n") .. "\n")
end

-- { {playerid, before, after}, ... }, or nil when the file is missing, cut short or not ours
local function read_data(path)
    local text = util.file_exists(path) and util.read_file(path) or nil
    if not text then return nil end
    local want = tonumber(text:match("^#turbo%-overall%-undo %-?%d+ (%d+)"))
    if not want then return nil end
    local entries = {}
    for line in text:gmatch("[^\r\n]+") do
        local pid, b, a = line:match("^(%-?%d+) (%-?%d+) (%-?%d+)$")
        if pid then entries[#entries + 1] = { math.tointeger(tonumber(pid)), math.tointeger(tonumber(b)), math.tointeger(tonumber(a)) } end
    end
    if #entries ~= want then return nil end
    return entries
end

local function remove_file(path)
    if type(os) == "table" and type(os.remove) == "function" then pcall(os.remove, path) end
end

-- Which database a run wrote: a career or the main-menu squads, the user's club, the bridge's Lua session and load
-- generation (bridge.lua write_state: a new one each time a career is loaded, entered or left)
local function career_identity()
    local in_cm = game.in_cm()
    local team = in_cm and (util.to_int(game.user_team_id()) or 0) or 0
    local b = type(TURBO_STATE) == "table" and type(TURBO_STATE.bridge) == "table" and TURBO_STATE.bridge or nil
    return {
        in_cm = in_cm,
        user_team = team,
        club = team > 0 and game.team_name(team) or nil,
        session = b and type(b.session) == "string" and b.session or nil,
        load_gen = b and util.to_int(b.load_gen) or nil,
    }
end
M._career_identity = career_identity

local function where(run)
    if run.in_cm == true then return "the career with " .. tostring(run.club or run.user_team) end
    return "the main-menu squads"
end

-- Records a run's overalls (entries {playerid, before, after}); nil when recorded (or nothing to record), else why not
local function save_run(out_dir, entries)
    if #entries == 0 then return nil end
    local doc, path, err = read_index(out_dir)
    if not doc then return err end
    local id = util.to_int(doc.next_id) or 1
    for _, r in ipairs(doc.runs) do
        local rid = util.to_int(r.id) or 0
        if rid >= id then id = rid + 1 end
    end
    local dpath = data_path(out_dir, id)
    local ok, werr = write_data(dpath, id, entries)
    if not ok then return "undo file not written: " .. tostring(werr) end
    local run = career_identity()
    run.id, run.when, run.count = id, os.date("%Y-%m-%d %H:%M:%S"), #entries
    doc.runs[#doc.runs + 1] = run
    doc.next_id = id + 1
    local total = 0
    for _, r in ipairs(doc.runs) do total = total + (util.to_int(r.count) or 0) end
    while #doc.runs > 1 and (#doc.runs > M.UNDO_RUNS or total > M.UNDO_MAX_ENTRIES) do
        local old = table.remove(doc.runs, 1)
        total = total - (util.to_int(old.count) or 0)
        remove_file(data_path(out_dir, old.id))
    end
    local okw, werr2 = write_index(path, doc)
    if not okw then
        remove_file(dpath)
        return "undo file not written: " .. tostring(werr2)
    end
    return nil
end

-- Puts the last run's overalls back where the overall is still the one the run wrote, only in the database the run
-- wrote (career or squads, the same club; another load of it only with "any_load": true). The run is dropped only when
-- every overall in it is settled (put back, or changed since); a run that matched nothing is kept as it is.
local function undo_overall(ctx)
    local players, perr = db.get_table("players")
    if not players then return false, perr end
    local doc, path, err = read_index(ctx.out_dir)
    if not doc then return false, err end
    local run = doc.runs[#doc.runs]
    if not run then return true, "no overall follow to undo" end
    local label = string.format("bulk edit of %s in %s, %d overalls", tostring(run.when), where(run), util.to_int(run.count) or 0)
    local now = career_identity()
    if (run.in_cm == true) ~= now.in_cm then
        return false, string.format("the last overall follow (%s) was not made here: %s first; the run is kept", label,
            run.in_cm == true and "load that career" or "leave the career for the main-menu squads")
    end
    if now.in_cm and (util.to_int(run.user_team) or 0) ~= now.user_team then
        return false, string.format("the last overall follow (%s) belongs to another career (you manage %s now): load that career; the run is kept",
            label, tostring(now.club or now.user_team))
    end
    local same_load = run.session == nil or now.session == nil
        or (run.session == now.session and util.to_int(run.load_gen) == now.load_gen)
    if not same_load and ctx.cfg.any_load ~= true then
        return false, string.format("the last overall follow (%s) was made before this %s was loaded: if it is the same save, put them back anyway (\"any_load\": true); the run is kept",
            label, now.in_cm and "career" or "database")
    end
    local dpath = data_path(ctx.out_dir, run.id)
    local entries = read_data(dpath)
    if not entries then return false, "undo data " .. dpath .. " is missing or not readable (left as it is); the run is kept" end
    local index = {}
    for i, e in ipairs(entries) do index[e[1]] = i end
    local back, changed, settled, failed = 0, 0, {}, {}
    for rec in db.records(players) do
        local i = index[db.get_int(players, rec, "playerid", -1)]
        if i and not settled[i] then
            local e = entries[i]
            if db.get_int(players, rec, "overallrating", 0) == e[3] then
                local ok, werr = db.set(players, rec, "overallrating", e[2], ctx.dry)
                if ok then
                    back, settled[i] = back + 1, true
                else
                    failed[#failed + 1] = string.format("%d (%s)", e[1], tostring(werr))
                end
            else
                changed, settled[i] = changed + 1, true
            end
        end
    end
    local remaining = {}
    for i, e in ipairs(entries) do
        if not settled[i] then remaining[#remaining + 1] = e end
    end
    local tail = ""
    if changed > 0 then tail = tail .. string.format("; %d changed since, left as they are", changed) end
    if #remaining - #failed > 0 then tail = tail .. string.format("; %d not in this database", #remaining - #failed) end
    if #failed > 0 then tail = tail .. "; not written: " .. table.concat(failed, ", ") end
    if ctx.dry then return true, string.format("DRY RUN: %d overalls would go back (%s)%s", back, label, tail) end
    if back == 0 then
        return #failed == 0, string.format("0 overalls put back: nothing matched in this database (%s)%s; the run is kept", label, tail)
    end
    local msg = string.format("%d overalls put back (%s)%s", back, label, tail)
    local werr
    if #remaining == 0 then
        doc.runs[#doc.runs] = nil
        local ok, e = write_index(path, doc)
        if ok then remove_file(dpath) else werr = e end
    else
        local ok, e = write_data(dpath, util.to_int(run.id) or -1, remaining)
        if ok then
            run.count = #remaining
            ok, e = write_index(path, doc)
        end
        if ok then msg = msg .. string.format("; the run keeps the other %d", #remaining) else werr = e end
    end
    if werr then msg = msg .. "; undo file not updated: " .. tostring(werr) end
    return #failed == 0, msg
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
    local followed = {}   -- {playerid, before, after}
    -- a refused write stops the run: the overalls already followed are recorded first, so they can still be put back
    local function stop(msg)
        if ctx.dry or #followed == 0 then return false, msg end
        local uerr = save_run(ctx.out_dir, followed)
        return false, msg .. string.format("; the %d overalls followed before it %s", #followed,
            uerr and ("could NOT be recorded: " .. uerr) or "are in overall_follow_undo.json (Put the overalls back)")
    end
    local api_fail, dev_added, plans, plan_fail = 0, 0, 0, {}
    for _, p in ipairs(list) do
        local a0, pos0, ovr0
        if follow then
            a0, pos0 = overall.read(db, players, p.rec)
            ovr0 = a0 and db.get_int(players, p.rec, "overallrating", 0)
        end
        for _, w in ipairs(writes) do
            local ok, werr = db.set(players, p.rec, w.field, w.value, ctx.dry)
            if not ok then return stop(string.format("player %d: %s", p.pid, werr)) end
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
                if not ok then return stop(string.format("player %d: overall %d -> %d: %s", p.pid, ovr0, want, werr)) end
                followed[#followed + 1] = { p.pid, ovr0, want }
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
            local uerr = save_run(ctx.out_dir, followed)
            summary = summary .. (uerr and ("; " .. uerr) or " (undo: overall_follow_undo.json)")
        end
    end
    if follow_note then summary = summary .. "; " .. follow_note end
    if api_fail > 0 then summary = summary .. string.format("; %d API calls failed", api_fail) end
    if #plan_fail > 0 then summary = summary .. "; development plan NOT updated for " .. table.concat(plan_fail, ", ") end
    return api_fail == 0 and #plan_fail == 0, summary
end

return M
