-- FC 27 LE Turbo: Remove every team transfer ban.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("transfer_bans", { mode = "unban_all_teams" })
