-- FC 27 LE Turbo: Record FC 27 players that have their own head model (run on an unmodified database).
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("headmodels", { mode = "capture" })
