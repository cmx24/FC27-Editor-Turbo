-- Turbo feature: player callnames (the name the commentary speaks). The GUI's Players > Callname tab sends these;
-- they are also usable from turbo_config.json:
--   "callnames": { "actions": [
--     { "action": "set_playernamemap", "playerid": 1002, "commentaryid": 930671 },  -- player-specific callname: the
--                                                                                   -- playernamemap row is updated or added
--     { "action": "remove_playernamemap", "playerid": 1002 },                       -- back to the common/last name rule
--     { "action": "set_display_name", "playerid": 1002, "firstname": "Yuri", "surname": "Alberto", "commonname": "" },
--                                                   -- editedplayernames row updated or added (the shown name)
--     { "action": "set_name_ids", "playerid": 1002, "lastnameid": 39795 }           -- and/or "commonnameid"
--   ] }
-- Game rule: playernamemap (playerid -> commentaryid) wins; otherwise the commentary id of the player's common name
-- (players.commonnameid -> playernames.commentaryid), else of his last name. commentaryid 900000 = no callname.
-- Rows are added through Live Editor's InsertDBTableRow and removed through DeleteDBTableRowByAddr (the record
-- address as a decimal string, as core/moves.lua does). Every write is range-checked (core/db.lua).
-- All actions are validated before the first one runs; ctx.dry validates only.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'

local M = {}

M.NO_CALLNAME = 900000
M.CALLNAME_MIN = 900000
M.CALLNAME_MAX = 965000
-- Live Editor's InsertDBTableRow crashes the game when the table is full ("Reached max rows"; FC 27's playernamemap is
-- full at 106 rows) and Lua cannot read a table's capacity, so a row is added only when the action says "room": true,
-- which the Turbo window sets after reading the table header (core/t3db.h Database::rows_in_use)
M.NO_ROOM = "adding a row needs \"room\": true from the Turbo window, which checks the table has room first (Live Editor crashes the game on a full table)"

local NAME_FIELDS = { "firstname", "surname", "commonname", "playerjerseyname" }

local function player_rec(pid)
    local players = db.get_table("players")
    if not players then return nil, "players table not available" end
    local rec = db.find(players, "playerid", pid)
    if not rec then return nil, string.format("player %d not found", pid) end
    return rec, players
end

local function insert_row(table_name, row, key_field, key_value)
    local insert = _G["InsertDBTableRow"]
    if type(insert) ~= "function" then return false, "InsertDBTableRow is not available in this Live Editor build" end
    local okc, res = pcall(insert, table_name, row)
    local tbl = db.get_table(table_name)
    if not okc or type(res) ~= "table" or not tbl or not db.find(tbl, key_field, key_value) then
        return false, string.format("could not add the %s row: %s", table_name, tostring(res))
    end
    return true
end

local function delete_rec(table_name, rec)
    local del = _G["DeleteDBTableRowByAddr"]
    if type(del) ~= "function" then return false, "DeleteDBTableRowByAddr is not available in this Live Editor build" end
    local okd, res = pcall(del, table_name, string.format("%d", rec))
    if not okd or res == false then return false, string.format("deleting a %s row failed: %s", table_name, tostring(res)) end
    return true
end

-- Validate one action; returns the normalized action or nil, error
local function check(a, i)
    if type(a) ~= "table" then return nil, string.format("action %d is not an object", i) end
    local kind = a.action
    local pid = util.to_int(a.playerid)
    if not pid then return nil, string.format("action %d: playerid missing", i) end
    local rec, perr = player_rec(pid)
    if not rec then return nil, string.format("action %d: %s", i, perr) end
    local out = { action = kind, playerid = pid, room = a.room == true }

    if kind == "set_playernamemap" then
        local cid = util.to_int(a.commentaryid)
        if not cid or cid < M.CALLNAME_MIN or cid > M.CALLNAME_MAX then
            return nil, string.format("action %d: commentaryid must be %d..%d", i, M.CALLNAME_MIN, M.CALLNAME_MAX)
        end
        local tbl = db.get_table("playernamemap")
        if not tbl or not db.has_fields(tbl, { "playerid", "commentaryid" }) then
            return nil, string.format("action %d: playernamemap table not available", i)
        end
        local v, verr = db.validate(tbl, "commentaryid", cid)
        if v == nil then return nil, string.format("action %d: %s", i, verr) end
        v, verr = db.validate(tbl, "playerid", pid)
        if v == nil then return nil, string.format("action %d: %s", i, verr) end
        out.commentaryid = cid
        return out
    elseif kind == "remove_playernamemap" then
        local tbl = db.get_table("playernamemap")
        if not tbl or not db.has_field(tbl, "playerid") then
            return nil, string.format("action %d: playernamemap table not available", i)
        end
        return out
    elseif kind == "set_display_name" then
        local tbl = db.get_table("editedplayernames")
        if not tbl or not db.has_field(tbl, "playerid") then
            return nil, string.format("action %d: editedplayernames table not available", i)
        end
        out.names = {}
        local any = false
        for _, f in ipairs(NAME_FIELDS) do
            if a[f] ~= nil then
                if type(a[f]) ~= "string" then return nil, string.format("action %d: %s must be text", i, f) end
                if db.has_field(tbl, f) then
                    local v, verr = db.validate(tbl, f, a[f])
                    if v == nil then return nil, string.format("action %d: %s", i, verr) end
                    out.names[f] = v
                    any = true
                end
            end
        end
        if not any then return nil, string.format("action %d: no name given (firstname, surname, commonname)", i) end
        return out
    elseif kind == "set_name_ids" then
        local _, players = player_rec(pid)
        out.ids = {}
        local any = false
        for _, f in ipairs({ "lastnameid", "commonnameid", "firstnameid" }) do
            if a[f] ~= nil then
                if not db.has_field(players, f) then return nil, string.format("action %d: players has no %s", i, f) end
                local v, verr = db.validate(players, f, a[f])
                if v == nil then return nil, string.format("action %d: %s", i, verr) end
                out.ids[f] = v
                any = true
            end
        end
        if not any then return nil, string.format("action %d: no name id given (lastnameid, commonnameid, firstnameid)", i) end
        return out
    end
    return nil, string.format("action %d: unknown action %s", i, tostring(kind))
end

local function apply(a, dry)
    local pid = a.playerid
    if a.action == "set_playernamemap" then
        local tbl = db.get_table("playernamemap")
        local rec = db.find(tbl, "playerid", pid)
        if dry then
            return true, string.format("player %d: callname %d (%s)", pid, a.commentaryid, rec and "row updated" or "row added")
        end
        if rec then
            local ok, err = db.set(tbl, rec, "commentaryid", a.commentaryid, false)
            if not ok then return false, err end
            return true, string.format("player %d: callname %d (row updated)", pid, a.commentaryid)
        end
        if not a.room then return false, string.format("player %d: no playernamemap row added: %s", pid, M.NO_ROOM) end
        local ok, err = insert_row("playernamemap", { playerid = tostring(pid), commentaryid = tostring(a.commentaryid) }, "playerid", pid)
        if not ok then return false, err end
        return true, string.format("player %d: callname %d (row added)", pid, a.commentaryid)
    elseif a.action == "remove_playernamemap" then
        local tbl = db.get_table("playernamemap")
        local recs = {}
        for rec in db.records(tbl) do
            if tbl:GetRecordFieldValue(rec, "playerid") == pid then recs[#recs + 1] = rec end
        end
        if #recs == 0 then return true, string.format("player %d: no player-specific callname", pid) end
        if dry then return true, string.format("player %d: %d playernamemap row(s) removed", pid, #recs) end
        for _, rec in ipairs(recs) do
            local ok, err = delete_rec("playernamemap", rec)
            if not ok then return false, err end
        end
        return true, string.format("player %d: %d playernamemap row(s) removed", pid, #recs)
    elseif a.action == "set_display_name" then
        local tbl = db.get_table("editedplayernames")
        local rec = db.find(tbl, "playerid", pid)
        local fields = {}
        for f, _ in pairs(a.names) do fields[#fields + 1] = f end
        table.sort(fields)
        if dry then
            return true, string.format("player %d: display name %s (%s)", pid, table.concat(fields, ", "), rec and "row updated" or "row added")
        end
        if rec then
            for _, f in ipairs(fields) do
                local ok, err = db.set(tbl, rec, f, a.names[f], false)
                if not ok then return false, err end
            end
            return true, string.format("player %d: display name updated (%s)", pid, table.concat(fields, ", "))
        end
        if not a.room then return false, string.format("player %d: no editedplayernames row added: %s", pid, M.NO_ROOM) end
        local row = { playerid = tostring(pid) }
        for _, f in ipairs(NAME_FIELDS) do
            if db.has_field(tbl, f) then row[f] = a.names[f] or "" end
        end
        local ok, err = insert_row("editedplayernames", row, "playerid", pid)
        if not ok then return false, err end
        return true, string.format("player %d: display name added (%s)", pid, table.concat(fields, ", "))
    elseif a.action == "set_name_ids" then
        local rec, players = player_rec(pid)
        if not rec then return false, players end
        local parts = {}
        for f, v in pairs(a.ids) do
            local ok, err = db.set(players, rec, f, v, dry)
            if not ok then return false, err end
            parts[#parts + 1] = string.format("%s=%d", f, v)
        end
        table.sort(parts)
        return true, string.format("player %d: %s", pid, table.concat(parts, ", "))
    end
    return false, "unknown action"
end

function M.run(ctx)
    local actions = ctx.cfg.actions
    if type(actions) ~= "table" or #actions == 0 then return true, "callnames: nothing to do (no actions)" end
    if not db.available() then return false, "database not available" end
    local plan = {}
    for i, a in ipairs(actions) do
        local out, err = check(a, i)
        if not out then return false, err end
        plan[#plan + 1] = out
    end
    local lines = {}
    for _, a in ipairs(plan) do
        local ok, msg = apply(a, ctx.dry)
        if not ok then
            log.error("callnames: %s", tostring(msg))
            return false, string.format("%s (done before the error: %d of %d)", tostring(msg), #lines, #plan)
        end
        log.info("callnames: %s", msg)
        lines[#lines + 1] = msg
    end
    return true, table.concat(lines, "\n")
end

return M
