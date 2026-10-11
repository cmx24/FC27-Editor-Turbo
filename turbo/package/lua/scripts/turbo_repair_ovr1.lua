-- FC 27 LE Turbo: OVR-1 repair runner (the six Torino players corrupted to overall 1; features/repair_ovr1.lua).
--
-- DRY RUN BY DEFAULT. Nothing is written to the game unless the confirm flag is set right before the run.
--
--   Dry run (default): lists per player and field what a repair would write, writes nothing
--       dofile("C:/FC 27 Live Editor/lua/scripts/turbo_repair_ovr1.lua")
--   Write (repairs the corrupted target players; the current values are saved first, see Undo):
--       TURBO_REPAIR_CONFIRM = true
--       dofile("C:/FC 27 Live Editor/lua/scripts/turbo_repair_ovr1.lua")
--   Undo (puts back the values saved in turbo_output\repair_ovr1_undo_<pid>.json by the last repair):
--       TURBO_REPAIR_UNDO = true                                -- preview only, writes nothing
--       TURBO_REPAIR_UNDO = true; TURBO_REPAIR_CONFIRM = true   -- the restore itself
--       dofile("C:/FC 27 Live Editor/lua/scripts/turbo_repair_ovr1.lua")
--   One player only (with any of the above): TURBO_REPAIR_PLAYER = 220493
--
-- The flags count for ONE run: the script clears them when it finishes, so the next dofile is a dry run again.
-- Kill switch: turbo_output\repair_ovr1_off.txt blocks every write (repair and undo); the dry run still reports.
-- The automatic path (career events) is separate and runs only with turbo_output\repair_ovr1_on.txt present.
-- Output: turbo_output\REPAIR_REPORT.txt (every field, old -> new), Live Editor's log ("[Turbo] [OVR1-REPAIR] ..."),
-- and a message box with the summary.
--
-- Console functions after one dofile:
--   repair_ovr1()                      dry run, all targets
--   repair_ovr1(220493)                dry run, Barreca only
--   repair_ovr1(nil, true)             WRITE every corrupted target
--   repair_ovr1(220493, true)          WRITE Barreca
--   repair_ovr1_undo(220493)           preview the undo of Barreca
--   repair_ovr1_undo(220493, true)     undo Barreca
-- Targets: 199254 João Pedro, 216145 Izzo, 220491 Gyasi, 220493 Barreca, 243497 Walukiewicz, 244836 Segre
-- (presets in turbo_output\players\<name>_<pid>.json). Only players that are actually corrupted are written.

local log = require 'imports/turbo/core/log'
local util = require 'imports/turbo/core/util'
local repair = require 'imports/turbo/features/repair_ovr1'

local function run(pid, confirm, undo)
    local opts = { write = confirm == true, manual = true, only = pid }
    if undo then return repair.undo(opts) end
    return repair.check_and_repair(opts)
end

_G["repair_ovr1"] = function(pid, confirm) return run(pid, confirm, false) end
_G["repair_ovr1_undo"] = function(pid, confirm) return run(pid, confirm, true) end

-- the flags of this run; cleared at once so they never apply to a later run
local confirm = rawget(_G, "TURBO_REPAIR_CONFIRM") == true
local undo = rawget(_G, "TURBO_REPAIR_UNDO") == true
local only = rawget(_G, "TURBO_REPAIR_PLAYER")
TURBO_REPAIR_CONFIRM, TURBO_REPAIR_UNDO, TURBO_REPAIR_PLAYER = nil, nil, nil

local title = "Turbo OVR-1 Repair" .. (undo and " UNDO" or "") .. (confirm and "" or " (DRY RUN)")
local ok, res, msg = pcall(run, only, confirm, undo)
if not ok then
    log.warn("[OVR1-REPAIR] script error: %s", tostring(res))
    util.message_box(title, "Script error (nothing written): " .. tostring(res))
else
    local text = tostring(msg)
    if res ~= true and confirm then text = "NOT completed: " .. text end
    if not confirm then
        text = "DRY RUN - nothing written.\n" .. text
            .. "\n\nTo " .. (undo and "restore" or "write") .. ": set TURBO_REPAIR_CONFIRM = true"
            .. (undo and " and TURBO_REPAIR_UNDO = true" or "") .. ", then dofile this script again."
    end
    util.message_box(title, text)
end
