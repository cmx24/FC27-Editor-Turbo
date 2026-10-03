-- FC 27 LE Turbo - safe access to Live Editor's in-memory database (LE.db, t3db)
-- Every write is validated against the field's metadata (type, bit depth, minimum)
-- so a value can never silently wrap around inside the record.

local util = require 'imports/turbo/core/util'

local M = {}

local FIELD_TYPE_STRING = 0
local FIELD_TYPE_INT = 3
local FIELD_TYPE_FLOAT = 4

function M.available()
    return type(LE) == "table" and type(LE.db) == "table" and type(LE.db.GetTable) == "function"
end

-- Returns table object or nil, error
function M.get_table(name)
    if not M.available() then return nil, "Live Editor database API (LE.db) is not loaded" end
    local ok, tbl = pcall(function() return LE.db:GetTable(name) end)
    if not ok then return nil, "error opening table " .. name .. ": " .. tostring(tbl) end
    if tbl == nil then return nil, "table not found: " .. name end
    if type(tbl.fields) ~= "table" then return nil, "table has no field metadata: " .. name end
    return tbl
end

function M.has_field(tbl, field)
    return tbl ~= nil and type(tbl.fields) == "table" and tbl.fields[field] ~= nil
end

-- Returns true or false, list_of_missing_fields
function M.has_fields(tbl, fields)
    local missing = {}
    for _, f in ipairs(fields) do
        if not M.has_field(tbl, f) then missing[#missing + 1] = f end
    end
    return #missing == 0, missing
end

function M.field_names(tbl)
    local names = {}
    if tbl and type(tbl.fields) == "table" then
        for name in pairs(tbl.fields) do names[#names + 1] = name end
    end
    table.sort(names)
    return names
end

-- Field description: { name, type = "int"|"float"|"string", min, max, max_len }
function M.field_info(tbl, field)
    if not M.has_field(tbl, field) then return nil end
    local f = tbl.fields[field]
    local desc = f.fld_desc or {}
    local info = { name = field, raw_type = f.type }
    if f.type == FIELD_TYPE_INT then
        info.type = "int"
        local depth = tonumber(desc.depth) or 0
        local min = tonumber(desc.min) or 0
        info.min = math.tointeger(min) or min
        if depth >= 1 and depth <= 62 then
            info.max = info.min + (1 << depth) - 1
        end
    elseif f.type == FIELD_TYPE_FLOAT then
        info.type = "float"
    elseif f.type == FIELD_TYPE_STRING then
        info.type = "string"
        info.max_len = math.floor((tonumber(desc.depth) or 0) / 8)
    else
        info.type = "unknown"
    end
    return info
end

-- Iterate valid records: for rec in db.records(tbl) do ... end
-- The next record is fetched before the body runs, so the body may edit or invalidate `rec`.
function M.records(tbl)
    local rec = 0
    if tbl and (tonumber(tbl.written_records) or 0) > 0 then
        rec = tbl:GetFirstRecord() or 0
    end
    return function()
        if rec == nil or rec <= 0 then return nil end
        local current = rec
        rec = tbl:GetNextValidRecord() or 0
        return current
    end
end

function M.get(tbl, rec, field)
    if not M.has_field(tbl, field) then return nil end
    return tbl:GetRecordFieldValue(rec, field)
end

-- Validate a value for a field. Returns normalized_value or nil, error
function M.validate(tbl, field, value)
    local info = M.field_info(tbl, field)
    if not info then return nil, "no field " .. tostring(field) .. " in table " .. tostring(tbl and tbl.name) end
    if info.type == "int" then
        local iv = util.to_int(value)
        if iv == nil then return nil, string.format("%s expects an integer, got %s", field, tostring(value)) end
        if info.max and (iv < info.min or iv > info.max) then
            return nil, string.format("%s=%d is outside the field range %d..%d", field, iv, info.min, info.max)
        end
        return iv
    elseif info.type == "float" then
        local fv = tonumber(value)
        if fv == nil then return nil, string.format("%s expects a number, got %s", field, tostring(value)) end
        return fv + 0.0
    elseif info.type == "string" then
        if type(value) ~= "string" then return nil, string.format("%s expects text", field) end
        if info.max_len and #value > info.max_len then
            return nil, string.format("%s accepts at most %d bytes, got %d", field, info.max_len, #value)
        end
        return value
    end
    return nil, "unsupported field type for " .. field
end

-- Write one field. dry = true validates only. Returns true or false, error
function M.set(tbl, rec, field, value, dry)
    local v, err = M.validate(tbl, field, value)
    if v == nil then return false, err end
    if dry then return true end
    local ok, res = pcall(function() return tbl:SetRecordFieldValue(rec, field, v) end)
    if not ok then return false, tostring(res) end
    if res == false then return false, "Live Editor refused to write " .. field end
    return true
end

-- First record where field == value, or nil
function M.find(tbl, field, value)
    for rec in M.records(tbl) do
        if tbl:GetRecordFieldValue(rec, field) == value then return rec end
    end
    return nil
end

-- Clamp an integer to a field's range (used for "extend by N" style edits)
function M.clamp(tbl, field, value)
    local info = M.field_info(tbl, field)
    if not info or info.type ~= "int" or not info.max then return value end
    if value < info.min then return info.min end
    if value > info.max then return info.max end
    return value
end

return M
