-- Turbo feature: dump database tables to CSV (rows + a field list with value ranges).
-- Use it to find FC 27 field names and values before writing a db_edit or bulk_edit.
--   "export_table": { "tables": ["teams", "players"], "max_rows": 0 }   (0 = all rows)

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local csv = require 'imports/turbo/core/csv'

local M = {}

function M.run(ctx)
    if not ctx.out_dir then return false, "no writable output folder" end
    local names = ctx.cfg.tables
    if type(names) ~= "table" or #names == 0 then return false, "tables list is empty" end
    local max_rows = util.to_int(ctx.cfg.max_rows or 0) or 0

    local parts = {}
    for _, name in ipairs(names) do
        if type(name) ~= "string" or not name:match("^[%w_]+$") then return false, "invalid table name: " .. tostring(name) end
        local tbl, err = db.get_table(name)
        if not tbl then return false, err end
        local fields = db.field_names(tbl)

        local frows = {}
        for _, f in ipairs(fields) do
            local info = db.field_info(tbl, f)
            frows[#frows + 1] = { field = f, type = info.type, min = info.min, max = info.max, max_len = info.max_len }
        end

        local rows = {}
        for rec in db.records(tbl) do
            local row = {}
            for _, f in ipairs(fields) do row[f] = tbl:GetRecordFieldValue(rec, f) end
            rows[#rows + 1] = row
            if max_rows > 0 and #rows >= max_rows then break end
        end

        local base = util.join(ctx.out_dir, "turbo_table_" .. name)
        if not ctx.dry then
            local ok1, e1 = csv.write(base .. ".csv", fields, rows)
            if not ok1 then return false, "cannot write " .. base .. ".csv: " .. tostring(e1) end
            local ok2, e2 = csv.write(base .. "_fields.csv", { "field", "type", "min", "max", "max_len" }, frows)
            if not ok2 then return false, "cannot write " .. base .. "_fields.csv: " .. tostring(e2) end
        end
        parts[#parts + 1] = string.format("%s: %d rows, %d fields", name, #rows, #fields)
    end
    return true, table.concat(parts, "; ") .. " -> " .. ctx.out_dir
end

return M
