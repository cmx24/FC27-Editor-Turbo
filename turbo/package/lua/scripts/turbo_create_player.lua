-- FC 27 LE Turbo: Create a new player from modules.create_player in turbo_config.json (a copy of "source": { "playerid" },
-- a preset file "source": { "file" }, or "source": { "blank": true }) in club "teamid" (111592 = Free Agents).
-- Set "turbo": { "dry_run": true } to see the plan without writing. The Turbo window (Players tab) has dialogs for this.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("create_player")
