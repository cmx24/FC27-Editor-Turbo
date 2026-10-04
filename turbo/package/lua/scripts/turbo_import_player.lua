-- FC 27 LE Turbo: Import the preset file modules.player_presets.file (Live Editor preset CSV, FC 26 files too, or a
-- Turbo player JSON) onto the existing player modules.player_presets.playerid; "groups" picks what is copied
-- (profile, attributes, positions, playstyles, appearance, contract, names, miniface; empty = everything).
local TURBO = require 'imports/turbo/turbo'
TURBO.run("player_presets", { mode = "import" })
