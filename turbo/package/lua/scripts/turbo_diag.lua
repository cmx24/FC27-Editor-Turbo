-- FC 27 LE Turbo: diagnostics for the Lua Engine. Read-only: calls no game function that changes anything.
-- Run it from Live Editor (Features -> Lua Engine -> execute). It writes turbo_output\turbo_diag.log, logs the same
-- lines to Live Editor's log ("[Turbo diag]") and shows a short summary in a message box.
local lines = {}
local function add(s)
    lines[#lines + 1] = tostring(s)
    if type(Log) == "function" then pcall(Log, "[Turbo diag] " .. tostring(s)) end
end

local function describe(name, v)
    local t = type(v)
    local extra = ""
    if t == "userdata" or t == "table" then
        local mt = getmetatable(v)
        extra = " metatable=" .. type(mt)
        if type(mt) == "table" then
            local keys = {}
            for k in pairs(mt) do keys[#keys + 1] = tostring(k) end
            table.sort(keys)
            extra = extra .. " {" .. table.concat(keys, ",", 1, math.min(#keys, 30)) .. "}"
        end
    end
    add(string.format("%s: %s%s", name, t, extra))
end

add("turbo_diag.lua started; _VERSION=" .. tostring(_VERSION))
for _, n in ipairs({ "require", "io", "os", "package", "string", "table", "pcall", "debug", "Log", "MessageBox",
                     "GetDBMeta", "GetPlugin", "GetDBTablesNames", "GetDBTableFields", "GetDBTableRows",
                     "IsInCM", "AddEventHandler", "ReadPointer", "ReadInt", "LOGGER", "LE", "MEMORY" }) do
    describe(n, _G[n])
end
add("package.path=" .. tostring(package and package.path))
add("package.cpath=" .. tostring(package and package.cpath))
add("package.loadlib=" .. tostring(package and package.loadlib))
if type(debug) == "table" and type(debug.getinfo) == "function" then
    local info = debug.getinfo(1, "S")
    add("this script source: " .. (type(info and info.source) == "string" and (info.source:sub(1, 1) == "@" and info.source or ("string chunk, " .. #info.source .. " bytes")) or "?"))
end

local okreq, trace = pcall(require, 'imports/turbo/core/trace')
add("require imports/turbo/core/trace: " .. tostring(okreq) .. " " .. (okreq and type(trace) or tostring(trace)))

if type(IsInCM) == "function" then
    local ok, v = pcall(IsInCM)
    add("IsInCM() -> " .. tostring(ok) .. " " .. tostring(v))
end

if type(GetDBMeta) == "function" then
    local ok, m = pcall(GetDBMeta)
    add("GetDBMeta() ok=" .. tostring(ok))
    if ok then
        describe("meta", m)
        for _, k in ipairs({ "field_desc_map", "shortname_name_tables_map", "tables" }) do
            local okk, v = pcall(function() return m[k] end)
            if okk then describe("meta." .. k, v) else add("meta." .. k .. " index error: " .. tostring(v)) end
            if okk and v ~= nil then
                local okp, it = pcall(pairs, v)
                add("  pairs(meta." .. k .. ") -> " .. tostring(okp) .. (okp and "" or (" " .. tostring(it))))
                if okp then
                    local n, sample = 0, {}
                    local okl, lerr = pcall(function()
                        for kk, vv in pairs(v) do
                            n = n + 1
                            if n <= 3 then sample[#sample + 1] = tostring(kk) .. "=" .. type(vv) end
                        end
                    end)
                    add("  entries=" .. n .. " loop_ok=" .. tostring(okl) .. " " .. (okl and "" or tostring(lerr))
                        .. " sample: " .. table.concat(sample, ", "))
                end
            end
        end
        local okf, fm = pcall(function() return m.field_desc_map end)
        local okn, nm = pcall(function() return m.shortname_name_tables_map end)
        if okn and nm ~= nil then
            for sn, name in pairs(nm) do
                if name == "players" then
                    add("players shortname=" .. tostring(sn))
                    if okf and fm ~= nil then
                        local okd, d = pcall(function() return fm[sn] end)
                        describe("field_desc_map[players]", okd and d or nil)
                        if okd and type(d) == "table" then
                            local c = 0
                            for fk, fv in pairs(d) do
                                c = c + 1
                                if c <= 2 then
                                    local parts = {}
                                    if type(fv) == "table" then
                                        for a, b in pairs(fv) do parts[#parts + 1] = tostring(a) .. "=" .. tostring(b) end
                                    end
                                    add("  field " .. tostring(fk) .. " (" .. type(fv) .. ") " .. table.concat(parts, " "))
                                end
                            end
                            add("  players fields=" .. c)
                        end
                    end
                    break
                end
            end
        end
    else
        add("GetDBMeta error: " .. tostring(m))
    end
end

local get_fields = _G["GetDBTableFields"]
if type(get_fields) == "function" then
    local ok, f = pcall(get_fields, "players")
    add("GetDBTableFields(players) ok=" .. tostring(ok) .. " type=" .. type(f) .. " n=" .. tostring(ok and type(f) == "table" and #f or "-"))
    if ok and type(f) == "table" and f[1] then
        local parts = {}
        local okp = pcall(function()
            for a, b in pairs(f[1]) do parts[#parts + 1] = tostring(a) .. "=" .. tostring(b) end
        end)
        if not okp then describe("fields[1]", f[1]) end
        table.sort(parts)
        add("  fields[1]: " .. table.concat(parts, " "))
    end
end
-- Compressed text fields (field type 13, e.g. playernames.name) are decoded by Live Editor's GetDBTableRows only
local get_rows = _G["GetDBTableRows"]
if type(get_rows) == "function" then
    local t0 = os.clock()
    local ok, rows = pcall(get_rows, "playernames")
    local dt = os.clock() - t0
    add(string.format("GetDBTableRows(playernames) ok=%s type=%s n=%s in %.2f s", tostring(ok), type(rows),
        tostring(ok and type(rows) == "table" and #rows or "-"), dt))
    if ok and type(rows) == "table" and rows[1] then
        local r = rows[1]
        local parts = {}
        pcall(function()
            for k, v in pairs(r) do
                local val = type(v) == "table" and v.value or v
                parts[#parts + 1] = tostring(k) .. "=" .. tostring(val) .. "(" .. type(type(v) == "table" and v.value or v) .. ")"
            end
        end)
        table.sort(parts)
        add("  rows[1]: " .. table.concat(parts, " "))
        if rows[2] then
            local okn, nm = pcall(function() return rows[2]["name"]["value"] .. " / nameid " .. tostring(rows[2]["nameid"]["value"]) end)
            add("  rows[2]: " .. tostring(okn and nm or "?"))
        end
        add("  rows[1].addr=" .. tostring(r.addr))
    end
end

if type(GetPlugin) == "function" then
    pcall(require, 'imports/services/enums')
    local okp, plug = pcall(function() return GetPlugin(_G["ENUM_djb2Database_CLSS"]) end)
    add("GetPlugin(Database) ok=" .. tostring(okp) .. " -> " .. (okp and string.format("%s", tostring(plug)) or tostring(plug)))
end

local okw, werr = pcall(function()
    local dir = "turbo_output"
    local okenv, env = pcall(require, 'imports/turbo/core/env')
    if okenv and type(env) == "table" and type(env.output_dir) == "function" then
        local okd, d = pcall(env.output_dir)
        if okd and d then dir = d end
    end
    local path = dir .. "/turbo_diag.log"
    local f = io.open(path, "a")
    if not f then
        path = "C:/FC 27 Live Editor/turbo_output/turbo_diag.log"
        f = assert(io.open(path, "a"))
    end
    f:write(os.date("%Y-%m-%d %H:%M:%S"), "  ---- turbo_diag.lua\n")
    for _, l in ipairs(lines) do f:write(l, "\n") end
    f:close()
    add("written to " .. path)
end)
if not okw then add("could not write turbo_diag.log: " .. tostring(werr)) end

-- Live Editor's MessageBox formats its text like printf ("%" must be doubled): the check below shows ONE percent sign
-- when that is so. Nothing else is passed to it unescaped.
if type(MessageBox) == "function" then
    pcall(MessageBox, "Turbo diagnostics", "Written to turbo_output\\turbo_diag.log (" .. #lines .. " lines).\n"
        .. "Percent check: 50%% (one percent sign = Live Editor formats message box text)")
end
