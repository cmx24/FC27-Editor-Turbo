-- FC 27 LE Turbo: Run the transfer/loan/release/list actions in modules.player_moves.actions.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("player_moves")
