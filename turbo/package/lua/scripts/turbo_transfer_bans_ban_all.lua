-- FC 27 LE Turbo: Transfer-ban every team until modules.transfer_bans.ban_until.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("transfer_bans", { mode = "ban_all_teams" })
