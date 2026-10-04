-- FC 27 LE Turbo: job security and unsackable for your manager (Manager Career). Feature-flagged: set
-- "manager_rules": {"enabled": true, "job_security": "safe", "unsackable": true, "confirm": true} in turbo_config.json,
-- then run this script (needs the Turbo GUI running in game: the game calls go through Turbo.dll).
local TURBO = require 'imports/turbo/turbo'
TURBO.run("manager_rules")
