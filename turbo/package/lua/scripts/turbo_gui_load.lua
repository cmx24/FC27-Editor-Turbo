-- FC 27 LE Turbo: load the Turbo GUI (turbo\Turbo.dll) into the running game. Show/hide it with F8.
-- Turbo never loads the GUI while the game is launching: run this script from Live Editor's Lua Engine once the
-- game is up (main menu or career). Each step is logged to turbo_output\turbo_boot.log.
local TURBO = require 'imports/turbo/turbo'
local bridge = require 'imports/turbo/bridge'
TURBO.boot()
local ok, msg = bridge.start()
if type(MessageBox) == "function" then
    pcall(MessageBox, "Turbo GUI", ok and (msg .. "\nPress F8 in game to show or hide it.\nIt starts a few seconds after loading; see turbo_output\\turbo_gui.log.") or msg)
end
