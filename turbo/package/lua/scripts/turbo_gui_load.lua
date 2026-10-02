-- FC 27 LE Turbo: connect the Turbo GUI to the game database now, and load turbo\Turbo.dll if it is not loaded yet.
-- With gui.autoload = true (default) you do not need this script: the GUI loads by itself while the game starts, is ready
-- about 20 seconds after the main menu appears (press F8), and connects to the database when you enter a career.
-- Run it at the main menu to connect there, or with gui.autoload = false to load the GUI at all.
-- Every step is logged to turbo_output\turbo_boot.log, and the result is shown in a message box and in Live Editor's log.
local function report(text)
    if type(LOGGER) == "table" and type(LOGGER.LogInfo) == "function" then
        pcall(LOGGER.LogInfo, LOGGER, "[Turbo] " .. text)
    elseif type(Log) == "function" then
        pcall(Log, "[Turbo] " .. text)
    end
    local okmb, util = pcall(require, 'imports/turbo/core/util')
    if okmb then util.message_box("Turbo GUI", text) end
end

local okt, trace = pcall(require, 'imports/turbo/core/trace')
if okt then trace.step("turbo_gui_load.lua started") end

local okr, err = pcall(function()
    local TURBO = require 'imports/turbo/turbo'
    local bridge = require 'imports/turbo/bridge'
    TURBO.boot()
    local ok, msg = bridge.start()
    if ok then
        report(msg .. "\nPress F8 in game to show or hide the Turbo GUI.\n"
            .. "It appears a few seconds after loading; turbo_output\\turbo_gui.log shows each step.")
    else
        report("Turbo GUI problem:\n" .. tostring(msg))
    end
end)
if not okr then
    report("turbo_gui_load.lua failed: " .. tostring(err)
        .. "\nCheck that the Turbo files were unzipped into the Live Editor folder (lua\\libs\\v2\\imports\\turbo).")
end
if okt then trace.step("turbo_gui_load.lua finished") end
