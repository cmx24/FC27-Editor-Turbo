-- FC 27 LE Turbo: read-only probe of every body type code in use (counts, height / weight ranges, example players).
-- Writes turbo_output\bodytypes_fc27.json.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("bodytypes")
