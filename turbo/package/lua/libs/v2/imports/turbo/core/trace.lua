-- FC 27 LE Turbo - start-up breadcrumbs.
-- Every step is appended to <Live Editor>\turbo_output\turbo_boot.log BEFORE it runs and the file is closed
-- (flushed) each time, so if the game ever dies or hangs the last line shows what Turbo was doing.
-- Pure Lua file I/O: this module never calls a game native.

local env = require 'imports/turbo/core/env'
local util = require 'imports/turbo/core/util'

local M = {}

local MAX_BYTES = 64 * 1024

function M.path()
    local ok, root = pcall(env.le_root)
    if not ok or not root then return nil end
    return util.join(util.join(root, "turbo_output"), "turbo_boot.log")
end

function M.step(text)
    pcall(function()
        local path = M.path()
        if not path then return end
        local mode = "ab"
        local probe = io.open(path, "rb")
        if probe then
            local size = probe:seek("end")
            probe:close()
            if size and size > MAX_BYTES then mode = "wb" end
        end
        local f = io.open(path, mode)
        if not f then return end
        local stamp = (type(os) == "table" and type(os.date) == "function") and os.date("%Y-%m-%d %H:%M:%S") or "?"
        f:write(stamp, "  ", tostring(text), "\n")
        f:close()
    end)
end

return M
