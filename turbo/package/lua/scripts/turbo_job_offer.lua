-- FC 27 LE Turbo: create a job offer from a chosen club for your manager (Manager Career). Feature-flagged:
-- set "job_offer": {"enabled": true, "teamid": <club id>, "confirm": true} in turbo_config.json, then run this script.
local TURBO = require 'imports/turbo/turbo'
TURBO.run("job_offer")
