-- FC 27 LE Turbo: List active transfer bans (log + CSV).
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("transfer_bans", { mode = "list" })
