-- FC 27 LE Turbo: Dump the tables in modules.export_table.tables to CSV.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("export_table")
