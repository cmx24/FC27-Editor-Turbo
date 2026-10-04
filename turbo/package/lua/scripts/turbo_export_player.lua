-- FC 27 LE Turbo: Export the players listed in modules.player_presets.playerids (turbo_config.json) to Live Editor
-- preset CSV files (extensions\player_presets, readable by Live Editor's own "Import from preset") and Turbo player JSON
-- files (turbo_output\players, with the miniface). The Turbo window (Players tab, "Export...") does the same.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("player_presets", { mode = "export" })
