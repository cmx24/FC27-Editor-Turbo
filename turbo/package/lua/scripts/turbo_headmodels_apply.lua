-- FC 27 LE Turbo: Apply the captured head-model list.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("headmodels", { mode = "apply" })
