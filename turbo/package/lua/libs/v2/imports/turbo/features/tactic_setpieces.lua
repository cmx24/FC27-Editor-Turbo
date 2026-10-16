-- Turbo feature: tactic_setpieces (2.12.1). The set-piece / captain / pre-set substitution fields of YOUR club's team sheet (cm_teamsheets), read and
-- written as part of a tactic preset. The rest of a preset (Line Height, Defensive Approach, Build-Up Style, roles and focuses) goes through
-- the game's live team sheet (core/team_sheet.h, channel E); these 24 fields are database columns, so this module is the guarded writer.
--   "tactic_setpieces": { "mode": "get" | "set" | "undo", "values": { "penaltytakerid": 12345, ... }, "team": 95, "preset": "name", "confirm": true }
--
--   get   reads the 24 fields of your club's row, writes turbo_output\tactic_setpieces_current.json (the window's "Save current" reads it).
--   set   needs confirm = true and a career. Refuses (writes nothing) when: a field is not one of the 24, a value is not a whole number in the
--         column's range, a player is not on your club's team sheet (-1 = nobody is always allowed), "team" is given and is not your club, or the
--         club's row cannot be found. It writes turbo_output\tactic_setpieces_undo.json (the old values) BEFORE the first write, writes every field,
--         reads every field back, and when ANY value differs puts all of them back from the old values and reports failure.
--   undo  puts the journal's old values back, only for fields that still hold the value this module wrote; the journal is renamed *.done.
-- Both result lines are also written to turbo_output\tactic_setpieces_result.json.
-- NOT PROVEN (2.12.1): that the running game reads these columns without a career save and load (the live team sheet service keeps its own copy:
-- Edit Tactic / Set Pieces may show the old takers until the career is loaded again). The read-back below proves only the database.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local log = require 'imports/turbo/core/log'

local M = {}

M.FIELDS = {
    "captainid", "penaltytakerid", "leftfreekicktakerid", "rightfreekicktakerid", "longkicktakerid",
    "leftcornerkicktakerid", "rightcornerkicktakerid", "throwerleft", "throwerright",
    "cksupport1", "cksupport2", "cksupport3", "cksupport4", "cksupport5", "cksupport6", "cksupport7", "cksupport8", "cksupport9",
    "customsub0in", "customsub0out", "customsub1in", "customsub1out", "customsub2in", "customsub2out",
}
M.CURRENT, M.RESULT, M.JOURNAL = "tactic_setpieces_current.json", "tactic_setpieces_result.json", "tactic_setpieces_undo.json"

local FIELD_SET = {}
for _, f in ipairs(M.FIELDS) do FIELD_SET[f] = true end

local function json_lib()
    local ok, j = pcall(require, 'imports/external/json')
    if ok and type(j) == "table" then return j end
    return nil
end

-- The row of the user's team in cm_teamsheets: tbl, rec or nil, error
local function sheet_row(teamid)
    local tbl = db.get_table("cm_teamsheets")
    if not tbl or not db.has_field(tbl, "teamid") then return nil, nil, "cm_teamsheets is not in this database" end
    for rec in db.records(tbl) do
        if tbl:GetRecordFieldValue(rec, "teamid") == teamid then return tbl, rec end
    end
    return nil, nil, "no team sheet row for your club (team " .. tostring(teamid) .. ")"
end

-- The players on the sheet (playerid0..52, -1 / nil skipped): set, count
local function squad(tbl, rec)
    local set, n = {}, 0
    for i = 0, 52 do
        local f = "playerid" .. i
        if db.has_field(tbl, f) then
            local v = tbl:GetRecordFieldValue(rec, f)
            if v and v > 0 and not set[v] then set[v] = true; n = n + 1 end
        end
    end
    return set, n
end

-- The fields this database has: { name = value } and the list of names it lacks
local function read_fields(tbl, rec)
    local vals, missing = {}, {}
    for _, f in ipairs(M.FIELDS) do
        if db.has_field(tbl, f) then vals[f] = tbl:GetRecordFieldValue(rec, f) else missing[#missing + 1] = f end
    end
    return vals, missing
end
M.read_fields = read_fields

local function write_json(path, obj)
    local json = json_lib()
    if not json then return false end
    local ok, text = pcall(json.encode, obj)
    return ok and util.write_file(path, text) or false
end

local function read_json(path)
    local json = json_lib()
    local text = util.read_file(path)
    if not json or not text then return nil end
    local ok, v = pcall(json.decode, text)
    if ok and type(v) == "table" then return v end
    return nil
end

local function result(ctx, ok, msg, extra)
    local obj = extra or {}
    obj.ok, obj.message, obj.created = ok, msg, os.date("%Y-%m-%d %H:%M:%S")
    if ctx.out_dir then write_json(util.join(ctx.out_dir, M.RESULT), obj) end
    return ok, msg
end

local function same(a, b)
    for _, f in ipairs(M.FIELDS) do
        if a[f] ~= b[f] then return false, f end
    end
    return true
end

-- Validates `want` (name -> value) against the sheet; returns the normalised map or nil, reason
function M.plan(tbl, rec, want)
    if type(want) ~= "table" then return nil, "values must be an object of field = player id" end
    local members = squad(tbl, rec)
    local plan, n = {}, 0
    for f, v in pairs(want) do
        n = n + 1
        if not FIELD_SET[f] then return nil, "\"" .. tostring(f) .. "\" is not a set-piece field Turbo writes" end
        if not db.has_field(tbl, f) then return nil, f .. " is not a column of this database" end
        local iv, err = db.validate(tbl, f, v)
        if iv == nil then return nil, err end
        if iv ~= -1 and not members[iv] then
            return nil, string.format("%s: player %d is not on your team sheet (only your club's players can take set pieces)", f, iv)
        end
        plan[f] = iv
    end
    if n == 0 then return nil, "no values" end
    return plan
end

local function do_get(ctx, teamid)
    local tbl, rec, err = sheet_row(teamid)
    if not tbl then return result(ctx, false, err) end
    local vals, missing = read_fields(tbl, rec)
    local _, squad_n = squad(tbl, rec)
    local obj = { turbo_setpieces = 1, team = teamid, values = vals, squad = squad_n, missing = missing }
    if ctx.out_dir and not write_json(util.join(ctx.out_dir, M.CURRENT), obj) then
        return result(ctx, false, "the values were read but turbo_output\\" .. M.CURRENT .. " could not be written")
    end
    return result(ctx, true, string.format("set pieces read: %d fields of team %d (%d players on the sheet)%s", #M.FIELDS - #missing, teamid, squad_n,
        #missing > 0 and (", missing columns: " .. table.concat(missing, ", ")) or ""), obj)
end

local function do_set(ctx, teamid)
    local cfg = ctx.cfg
    if cfg.confirm ~= true then return result(ctx, false, "not written: confirm is not true") end
    if cfg.team ~= nil and util.to_int(cfg.team) ~= teamid then
        return result(ctx, false, string.format("not written: the preset was made for team %s, your club is team %d", tostring(cfg.team), teamid))
    end
    local tbl, rec, err = sheet_row(teamid)
    if not tbl then return result(ctx, false, err) end
    local plan, why = M.plan(tbl, rec, cfg.values)
    if not plan then return result(ctx, false, "not written: " .. why) end
    local before = read_fields(tbl, rec)
    local changes, n = {}, 0
    for f, v in pairs(plan) do
        if before[f] ~= v then changes[f] = v; n = n + 1 end
    end
    if n == 0 then return result(ctx, true, "set pieces: nothing to change, your sheet already holds these values", { team = teamid, changed = 0 }) end
    if ctx.dry then return result(ctx, true, string.format("dry run: %d set-piece field(s) would change", n), { team = teamid, changed = n }) end
    local after = {}
    for f in pairs(changes) do after[f] = changes[f] end
    local journal = { turbo_setpieces_undo = 1, team = teamid, preset = cfg.preset or "", created = os.date("%Y-%m-%d %H:%M:%S"), before = {}, after = after }
    for f in pairs(changes) do journal.before[f] = before[f] end
    local jpath = ctx.out_dir and util.join(ctx.out_dir, M.JOURNAL)
    if not jpath or not write_json(jpath, journal) then return result(ctx, false, "not written: the undo journal could not be saved first") end
    local function revert()
        for f, old in pairs(journal.before) do pcall(db.set, tbl, rec, f, old) end
    end
    for f, v in pairs(changes) do
        local ok, e = db.set(tbl, rec, f, v)
        if not ok then
            revert()
            return result(ctx, false, "write failed at " .. f .. " (" .. tostring(e) .. "); every field was put back")
        end
    end
    local now = read_fields(tbl, rec)
    for f, v in pairs(changes) do
        if now[f] ~= v then
            revert()
            local back = read_fields(tbl, rec)
            local restored = true
            for g2, old in pairs(journal.before) do if back[g2] ~= old then restored = false end end
            return result(ctx, false, string.format("read-back mismatch at %s (wanted %d, the database holds %s); %s", f, v, tostring(now[f]),
                restored and "every field was put back" or "WARNING: some fields could not be put back, see the undo journal"))
        end
    end
    log.info("tactic_setpieces: %d field(s) written and read back for team %d", n, teamid)
    return result(ctx, true, string.format("set pieces: %d field(s) written and read back from the database (the running game may show them only after a career save and load)", n),
        { team = teamid, changed = n, written = after, read_back = true })
end

local function do_undo(ctx, teamid)
    if ctx.cfg.confirm ~= true then return result(ctx, false, "not undone: confirm is not true") end
    local jpath = ctx.out_dir and util.join(ctx.out_dir, M.JOURNAL)
    local j = jpath and read_json(jpath)
    if not j or j.turbo_setpieces_undo ~= 1 or type(j.before) ~= "table" or type(j.after) ~= "table" then
        return result(ctx, false, "nothing to undo: no set-piece journal")
    end
    if util.to_int(j.team) ~= teamid then return result(ctx, false, "not undone: the journal belongs to team " .. tostring(j.team)) end
    local tbl, rec, err = sheet_row(teamid)
    if not tbl then return result(ctx, false, err) end
    local now = read_fields(tbl, rec)
    local put, left = 0, {}
    for f, old in pairs(j.before) do
        if FIELD_SET[f] and db.has_field(tbl, f) then
            if now[f] == j.after[f] then
                if ctx.dry then put = put + 1
                else
                    local ok = db.set(tbl, rec, f, old)
                    if ok and tbl:GetRecordFieldValue(rec, f) == old then put = put + 1 else left[#left + 1] = f end
                end
            else
                left[#left + 1] = f
            end
        end
    end
    if not ctx.dry and #left == 0 then os.rename(jpath, jpath .. ".done") end
    local msg = string.format("set pieces undone: %d field(s) put back%s", put, #left > 0 and ("; left alone (changed since or not written back): " .. table.concat(left, ", ")) or "")
    return result(ctx, #left == 0, msg, { team = teamid, put_back = put })
end

function M.run(ctx)
    if not game.in_cm() then return result(ctx, false, "the set pieces need a loaded career") end
    local teamid = game.user_team_id()
    if not teamid or teamid <= 0 then return result(ctx, false, "your club was not found") end
    local mode = ctx.cfg.mode or "get"
    if mode == "get" then return do_get(ctx, teamid) end
    if mode == "set" then return do_set(ctx, teamid) end
    if mode == "undo" then return do_undo(ctx, teamid) end
    return result(ctx, false, "mode must be get, set or undo")
end

return M
