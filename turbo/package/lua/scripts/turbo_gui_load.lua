-- FC 27 LE Turbo: load the Turbo GUI (turbo\Turbo.dll) into the game. Show/hide it with F8.
-- Turbo normally loads it by itself when Live Editor starts (turbo_config.json: "gui": {"autoload": true}).
local TURBO = require 'imports/turbo/turbo'
local bridge = require 'imports/turbo/bridge'
TURBO.boot()
local ok, msg = bridge.load_gui()
if type(MessageBox) == "function" then
    pcall(MessageBox, "Turbo GUI", ok and (msg .. "\nPress F8 in game to show or hide it.") or msg)
end
