-- FC 27 LE Turbo: Run the callname actions in modules.callnames.actions (playernamemap rows, display names, name ids).
-- Settings live in turbo_config.json (Live Editor folder). The Turbo GUI's Players > Callname tab sends the same actions.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("callnames")
