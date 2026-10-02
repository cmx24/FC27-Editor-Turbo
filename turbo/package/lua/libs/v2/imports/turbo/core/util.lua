-- FC 27 LE Turbo - small helpers (no Live Editor dependencies)

local M = {}

function M.is_int(v)
    return math.type(v) == "integer" or (type(v) == "number" and v == math.floor(v) and v == v and v ~= math.huge and v ~= -math.huge)
end

-- Convert a JSON number / numeric string to a Lua integer. Returns nil when not an integer.
function M.to_int(v)
    if type(v) == "string" then
        v = tonumber(v)
    end
    if type(v) ~= "number" then return nil end
    if not M.is_int(v) then return nil end
    return math.tointeger(v)
end

-- JSON objects always have string keys. Turn {"158023": 20801} into {[158023] = 20801}.
-- Returns map, list_of_bad_entries
function M.int_key_map(t)
    local out, bad = {}, {}
    if type(t) ~= "table" then return out, bad end
    for k, v in pairs(t) do
        local ik = M.to_int(k)
        local iv = M.to_int(v)
        if ik and iv then
            out[ik] = iv
        else
            bad[#bad + 1] = tostring(k)
        end
    end
    return out, bad
end

-- List of integers from a JSON array. Returns list, bad_count
function M.int_list(t)
    local out, bad = {}, 0
    if type(t) ~= "table" then return out, bad end
    for _, v in ipairs(t) do
        local iv = M.to_int(v)
        if iv then out[#out + 1] = iv else bad = bad + 1 end
    end
    return out, bad
end

function M.set_of(list)
    local s = {}
    for _, v in ipairs(list or {}) do s[v] = true end
    return s
end

function M.count(t)
    local n = 0
    if type(t) ~= "table" then return 0 end
    for _ in pairs(t) do n = n + 1 end
    return n
end

function M.sorted_keys(t)
    local keys = {}
    for k in pairs(t or {}) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
        if type(a) == type(b) then return a < b end
        return tostring(a) < tostring(b)
    end)
    return keys
end

function M.deep_copy(v)
    if type(v) ~= "table" then return v end
    local out = {}
    for k, x in pairs(v) do out[k] = M.deep_copy(x) end
    return out
end

-- Merge src onto dst (recursively for tables). Returns dst.
function M.deep_merge(dst, src)
    if type(src) ~= "table" then return dst end
    for k, v in pairs(src) do
        if type(v) == "table" and type(dst[k]) == "table" and not M.is_array(v) and not M.is_array(dst[k]) then
            M.deep_merge(dst[k], v)
        else
            dst[k] = M.deep_copy(v)
        end
    end
    return dst
end

function M.is_array(t)
    if type(t) ~= "table" then return false end
    local n = 0
    for k in pairs(t) do
        if math.type(k) ~= "integer" or k < 1 then return false end
        n = n + 1
    end
    for i = 1, n do
        if t[i] == nil then return false end
    end
    return n > 0
end

function M.trim(s)
    return (tostring(s):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Path join. Uses the separator already present in `a` (Windows paths from Live Editor use "\\").
function M.join(a, b)
    if a == nil or a == "" then return b end
    local last = a:sub(-1)
    if last == "\\" or last == "/" then
        return a .. b
    end
    local sep = "\\"
    if not a:find("\\", 1, true) and a:find("/", 1, true) then sep = "/" end
    return a .. sep .. b
end

function M.file_exists(path)
    if type(path) ~= "string" or path == "" then return false end
    local f = io.open(path, "rb")
    if f then
        f:close()
        return true
    end
    return false
end

function M.read_file(path)
    local f, err = io.open(path, "rb")
    if not f then return nil, err end
    local data = f:read("a")
    f:close()
    return data
end

function M.write_file(path, data)
    local f, err = io.open(path, "wb")
    if not f then return false, err end
    local ok, werr = f:write(data)
    f:close()
    if not ok then return false, werr end
    return true
end

-- Valid YYYYMMDD integer
function M.is_yyyymmdd(v)
    v = M.to_int(v)
    if not v then return false end
    local y = v // 10000
    local m = (v // 100) % 100
    local d = v % 100
    return y >= 1900 and y <= 2200 and m >= 1 and m <= 12 and d >= 1 and d <= 31
end

function M.timestamp_suffix(date)
    if type(date) == "table" and date.year then
        return string.format("%04d_%02d_%02d", date.year, date.month, date.day)
    end
    return os.date("%Y_%m_%d")
end

-- Gregorian day number (players.birthdate, playerjointeamdate: days since 1582-10-14) -> y, m, d.
-- Exact inverse of Live Editor's DATE:ToGregorianDays (core/date.lua). LE's DATE:FromGregorianDays
-- is not: it returns the day after for some dates (2000-02-29 comes back as 2000-03-01).
function M.date_from_gregorian_days(days)
    days = math.tointeger(days) or math.floor(days)
    local a = days + 2331204   -- Julian day number + 32044 (day 1 = 1582-10-15)
    local b = (4 * a + 3) // 146097
    local c = a - (146097 * b) // 4
    local d = (4 * c + 3) // 1461
    local e = c - (1461 * d) // 4
    local m = (5 * e + 2) // 153
    return 100 * b + d - 4800 + m // 10, m + 3 - 12 * (m // 10), e - (153 * m + 2) // 5 + 1
end

function M.gregorian_days_from_date(y, m, d)
    local a = (14 - m) // 12
    local mm = m + 12 * a - 3
    local yy = y + 4800 - a
    return d + (153 * mm + 2) // 5 + yy * 365 + yy // 4 - yy // 100 + yy // 400 - 2331205
end

return M
