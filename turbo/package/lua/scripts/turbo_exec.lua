-- FC 27 LE Turbo: run the command the Turbo GUI has queued right now.
-- Live Editor only sends events to Lua in Career Mode, so outside a career the GUI's Turbo Tools
-- wait for this script.
local TURBO = require 'imports/turbo/turbo'
local bridge = require 'imports/turbo/bridge'
TURBO.boot()
local ok, ran = pcall(bridge.poll_mailbox, true)
-- and the game images the Turbo window is waiting for
pcall(function() return (require 'imports/turbo/core/legacy').pump(5) end)
local msg
if not ok then
    msg = "Error: " .. tostring(ran)
elseif ran then
    msg = "Ran the queued Turbo GUI command. The result is shown in the Turbo GUI."
else
    msg = "No queued command from the Turbo GUI (or the GUI is not loaded)."
end
local okmb, util = pcall(require, 'imports/turbo/core/util')
if okmb then util.message_box("Turbo GUI", msg) end
