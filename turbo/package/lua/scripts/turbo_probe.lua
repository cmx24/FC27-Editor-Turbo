-- FC 27 LE Turbo: Read-only report: LE version, API natives, DB tables/fields, memory calibration. Run this first.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("probe")
