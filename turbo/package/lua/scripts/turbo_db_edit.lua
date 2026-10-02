-- FC 27 LE Turbo: Run the database edits in modules.db_edit.edits.
-- Settings live in turbo_config.json (Live Editor folder). Output goes to turbo_output.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("db_edit")
