-- FC 27 LE Turbo: Season transfer history to CSV in turbo_output.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("export_transfer_history")
