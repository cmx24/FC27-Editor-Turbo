-- FC 27 LE Turbo: runs while Live Editor initialises the game, so it does pure-Lua work only (see TURBO.boot):
-- no game natives, no memory access and no Turbo.dll. Each step is logged to turbo_output\turbo_boot.log.
-- Delete this file to switch Turbo's automatic features off completely.
local okt, trace = pcall(require, 'imports/turbo/core/trace')
if okt then trace.step("autorun: turbo_boot.lua started") end
local ok, TURBO = pcall(require, 'imports/turbo/turbo')
if ok then
    local booted, err = pcall(TURBO.boot, { at_launch = true })
    if not booted then
        local msg = "[Turbo] boot failed: " .. tostring(err)
        if type(Log) == "function" then Log(msg) else print(msg) end
    end
else
    local msg = "[Turbo] failed to load: " .. tostring(TURBO)
    if type(Log) == "function" then Log(msg) else print(msg) end
end
if okt then trace.step("autorun: turbo_boot.lua finished") end
