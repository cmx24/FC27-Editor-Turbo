-- Test harness: builds a Live Editor folder (real FC 27 LE Lua libs + Turbo package) in a temp
-- directory, installs the simulated natives, loads the LE v1 + v2 libraries exactly like Live
-- Editor does, then lets each test drive Turbo through its real entry points.

local H = {}

local function abs_dir(src)
    local d = src:sub(2):match("(.*/)") or "./"
    if d:sub(1, 1) ~= "/" then
        local pwd = os.getenv("PWD") or "."
        d = pwd .. "/" .. d:gsub("^%./", "")
    end
    return d
end
local HERE = abs_dir(debug.getinfo(1, "S").source)
H.ROOT = HERE:gsub("/tests/$", "")
package.path = HERE .. "mock/?.lua;" .. HERE .. "?.lua;" .. package.path

local Sim = require 'sim'

H.passed, H.failed, H.failures = 0, 0, {}

local function sh(cmd)
    local ok = os.execute(cmd)
    assert(ok, "command failed: " .. cmd)
end

-- Create <tmp>/LE with lua/libs from FC 27 LE + Turbo files merged on top
function H.make_le_dir(opts)
    opts = opts or {}
    local tmp = os.tmpname()
    os.remove(tmp)
    local le = tmp .. "_LE"
    sh(string.format("mkdir -p '%s/lua' && cp -r '%s/le27/libs' '%s/lua/' && cp -r '%s/package/.' '%s/'", le, H.ROOT, le, H.ROOT, le))
    if opts.no_config then os.remove(le .. "/turbo_config.json") end
    return le
end

-- opts: in_cm, config (table merged into turbo_config.json), no_config
-- Calling setup again starts a fresh Live Editor session (new folder, memory, Lua modules).
function H.setup(opts)
    opts = opts or {}
    if H.LE then os.execute(string.format("rm -rf '%s'", H.LE)) end
    for k in pairs(package.loaded) do
        if type(k) == "string" and k:match("^imports/") then package.loaded[k] = nil end
    end
    TURBO_STATE, LE, MEMORY, LOGGER = nil, nil, nil, nil
    local le = H.make_le_dir(opts)
    H.LE = le
    package.path = le .. "/lua/libs/v2/?.lua;" .. HERE .. "mock/?.lua;" .. HERE .. "?.lua;" .. package.path

    local sim = Sim.new()
    sim:install()
    if opts.le_27_1_2 then sim:as_le_27_1_2() end   -- only the natives FC 27 LE v27.1.2 has
    sim.in_cm = opts.in_cm == true
    LE_DATA_PATH = le
    H.sim = sim

    -- Live Editor start-up: API v1 then API v2
    dofile(le .. "/lua/libs/v1/live_editor.lua")
    dofile(le .. "/lua/libs/v2/main.lua")

    if opts.config then H.write_config(opts.config) end
    -- The Turbo GUI running in game (mailbox + readable-memory map), unless a test needs it absent
    if not opts.no_gui then sim:enable_gui(le) end
    return sim, le
end

-- Merge a table into the shipped turbo_config.json
function H.write_config(patch)
    local json = require 'imports/external/json'
    local path = H.LE .. "/turbo_config.json"
    local f = io.open(path, "rb")
    local cfg = f and json.decode(f:read("a")) or {}
    if f then f:close() end
    local function merge(dst, src)
        for k, v in pairs(src) do
            if type(v) == "table" and type(dst[k]) == "table" and next(v) ~= nil and #v == 0 then
                merge(dst[k], v)
            else
                dst[k] = v
            end
        end
    end
    merge(cfg, patch)
    local out = io.open(path, "wb")
    out:write(json.encode(cfg))
    out:close()
end

function H.script(name)
    return dofile(H.LE .. "/lua/scripts/" .. name .. ".lua")
end

function H.turbo()
    return require 'imports/turbo/turbo'
end

function H.read(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local d = f:read("a")
    f:close()
    return d
end

function H.out(name)
    return H.LE .. "/turbo_output/" .. name
end

function H.ls_out(pattern)
    local p = io.popen(string.format("ls '%s/turbo_output' 2>/dev/null", H.LE))
    local out = {}
    for line in p:lines() do
        if not pattern or line:match(pattern) then out[#out + 1] = line end
    end
    p:close()
    return out
end

function H.csv_lines(path)
    local d = H.read(path)
    if not d then return nil end
    local lines = {}
    for line in d:gmatch("([^\r\n]+)") do lines[#lines + 1] = line end
    return lines
end

------------------------------------------------------------------ assertions
local current = "?"
function H.case(name, fn)
    current = name
    local ok, err = xpcall(fn, debug.traceback)
    if ok then
        H.passed = H.passed + 1
        print("  PASS " .. name)
    else
        H.failed = H.failed + 1
        H.failures[#H.failures + 1] = name .. ": " .. tostring(err)
        print("  FAIL " .. name .. "\n" .. tostring(err))
    end
end

function H.eq(a, b, msg)
    if a ~= b then error(string.format("%s: expected %s, got %s", msg or "eq", tostring(b), tostring(a)), 2) end
end

function H.ok(v, msg)
    if not v then error(msg or "expected truthy", 2) end
end

function H.has(s, sub, msg)
    if type(s) ~= "string" or not s:find(sub, 1, true) then
        error(string.format("%s: %q not found in %q", msg or "has", sub, tostring(s)), 2)
    end
end

function H.finish()
    print(string.format("RESULT %d passed, %d failed", H.passed, H.failed))
    if H.LE then os.execute(string.format("rm -rf '%s'", H.LE)) end
    os.exit(H.failed == 0 and 0 or 1)
end

return H
