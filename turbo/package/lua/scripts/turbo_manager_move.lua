-- FC 27 LE Turbo: move a manager to another club or make him a free agent (career database). Feature-flagged: set
-- "manager_move": {"enabled": true, "managerid": <id>, "teamid": <club id, 0 = free agent>, "confirm": true} in
-- turbo_config.json, then run this script.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("manager_move")
