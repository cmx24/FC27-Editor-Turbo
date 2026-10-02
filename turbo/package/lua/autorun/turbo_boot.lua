-- FC 27 LE Turbo: loads Turbo and its automatic features when Live Editor starts.
local ok, TURBO = pcall(require, 'imports/turbo/turbo')
if ok then
    local booted, err = pcall(TURBO.boot)
    if not booted then
        local msg = "[Turbo] boot failed: " .. tostring(err)
        if type(Log) == "function" then Log(msg) else print(msg) end
    end
else
    local msg = "[Turbo] failed to load: " .. tostring(TURBO)
    if type(Log) == "function" then Log(msg) else print(msg) end
end
