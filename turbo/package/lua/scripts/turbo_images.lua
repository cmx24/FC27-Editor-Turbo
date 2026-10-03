-- FC 27 LE Turbo: load the game images the Turbo window is waiting for (minifaces, manager faces, tattoo previews).
-- Live Editor runs Lua only on career-mode events, so the Turbo window gets its images a few at a time; executing this
-- script in Live Editor's Lua Engine loads up to 20 seconds' worth at once (the game pauses meanwhile).
local legacy = require 'imports/turbo/core/legacy'
local util = require 'imports/turbo/core/util'
local ok, exported, missing, waiting = pcall(legacy.pump, 20)
local msg
if not ok then
    msg = "Error: " .. tostring(exported)
elseif exported == nil then
    msg = tostring(missing)
else
    msg = string.format("Images exported: %d. Not in the game: %d. Still waiting: %d%s", exported, missing, waiting,
        waiting > 0 and " (execute this script again for more)" or "")
end
util.message_box("Turbo images", msg)
