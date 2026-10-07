-- FC 27 LE Turbo: read-only probe of tactic-like tables, one cm_teamsheets row and the squad role list with ages.
-- Writes turbo_output\probe_tactics.json and probe_tactics.txt. Run it in a career where the squad role bug shows.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("probe_tactics")
