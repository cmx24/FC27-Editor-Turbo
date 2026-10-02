-- FC 27 LE Turbo - CSV writer (RFC 4180 quoting, UTF-8)

local M = {}

function M.cell(v)
    if v == nil then return "" end
    local s
    if math.type(v) == "float" then
        if v == math.floor(v) and math.abs(v) < 1e15 then
            s = string.format("%d", math.tointeger(v))
        else
            s = string.format("%.4f", v)
        end
    else
        s = tostring(v)
    end
    if s:find('[,"\r\n]') then
        s = '"' .. s:gsub('"', '""') .. '"'
    end
    return s
end

-- rows: list of tables keyed by column name
-- Returns true or false, error
function M.write(path, columns, rows)
    local f, err = io.open(path, "wb")
    if not f then return false, err end
    local header = {}
    for i, c in ipairs(columns) do header[i] = M.cell(c) end
    f:write(table.concat(header, ","), "\r\n")
    for _, row in ipairs(rows) do
        local line = {}
        for i, c in ipairs(columns) do line[i] = M.cell(row[c]) end
        f:write(table.concat(line, ","), "\r\n")
    end
    f:close()
    return true
end

return M
