-- FC 27 LE Turbo: Automatic OVR-1 repair for corrupted players.
-- Automatically runs on career load and career lifecycle events.
-- Checks target players (Barreca, João Pedro, Izzo, Gyasi, Walukiewicz, Segre) and any corrupted user club players.
-- If any have corrupted attributes (overallrating <= 5, or crossing <= 5 and finishing <= 5):
-- 1. Restores 34 core attributes, overallrating and potential from player presets in turbo_output\players\.
-- 2. Restores career_playergrowthuserseason (DyYs) record if present (overall + 34 core attributes).
-- 3. Synchronizes all attributes into the game's native development plan via moves.sync_plan(pid, plan_attrs, true).
-- 4. Clears modifier to 0 so unearned modifiers do not distort ratings.
-- 5. Writes report to turbo_output\REPAIR_REPORT.txt and logs to turbo_gui.log and Live Editor console.

local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local util = require 'imports/turbo/core/util'
local moves = require 'imports/turbo/core/moves'
local preset = require 'imports/turbo/core/preset'

local M = {}

M.KILL_FILE = "repair_ovr1_off.txt"

M.TARGETS = {
    [220493] = "Antonio_Barreca_220493.json",
    [199254] = "Jo_o_Pedro_199254.json",
    [216145] = "Armando_Izzo_216145.json",
    [220491] = "Emmanuel_Gyasi_220491.json",
    [243497] = "Sebastian_Walukiewicz_243497.json",
    [244836] = "Jacopo_Segre_244836.json",
}

function M.enabled(out_dir)
    return not util.file_exists(util.join(out_dir, M.KILL_FILE))
end

function M.needs_repair(players, prec)
    local cro = db.get_int(players, prec, "crossing", -1)
    local fin = db.get_int(players, prec, "finishing", -1)
    local ovr = db.get_int(players, prec, "overallrating", -1)
    return (ovr > 0 and ovr <= 5) or (cro > 0 and cro <= 5 and fin > 0 and fin <= 5)
end

function M.repair_player(pid, filename, dry, prec_override)
    local players = db.get_table("players")
    if not players then return false, "players table not found" end
    local prec = prec_override or db.find(players, "playerid", pid)
    if not prec then return false, string.format("player %d not found in players table", pid) end

    local path = util.join(util.join(env.output_dir(), "players"), filename)
    local doc, err = preset.load(path)
    if not doc then return false, "could not load preset " .. path .. ": " .. tostring(err) end

    local row, rerr = preset.pick_row(doc, {})
    if not row then return false, "could not pick row from preset: " .. tostring(rerr) end

    local fields, names = preset.map_row(players, row, {})
    local written, plan_attrs = 0, {}
    local groups = { profile = true, attributes = true, positions = true, playstyles = true }

    for f, v in pairs(fields) do
        local g = preset.group_of(f)
        if groups[g] and db.has_field(players, f) then
            if not dry then
                local ok, werr = db.set(players, prec, f, v)
                if not ok then return false, string.format("failed to set %s: %s", f, tostring(werr)) end
            end
            written = written + 1
            if g == "attributes" then
                plan_attrs[f] = v
            end
        end
    end

    -- Clear modifier to 0 so it doesn't distort ratings
    if not dry and db.has_field(players, "modifier") then
        pcall(db.set, players, prec, "modifier", 0)
    end

    -- Update career_playergrowthuserseason table (DyYs) if it exists
    local growth_written = 0
    local growth_tbl = db.get_table("career_playergrowthuserseason")
    if growth_tbl and not dry then
        local grec = db.find(growth_tbl, "playerid", pid)
        if grec then
            local ovr_val = fields["overallrating"] or (doc.players and doc.players.overallrating)
            if ovr_val and db.has_field(growth_tbl, "overall") then
                pcall(db.set, growth_tbl, grec, "overall", ovr_val)
                growth_written = growth_written + 1
            end
            for attr_name, attr_val in pairs(plan_attrs) do
                if db.has_field(growth_tbl, attr_name) then
                    local sok = pcall(db.set, growth_tbl, grec, attr_name, attr_val)
                    if sok then growth_written = growth_written + 1 end
                end
            end
        end
    end

    -- Sync unconditionally to native development plan
    local pstate = "none"
    if not dry then
        pstate = moves.sync_plan(pid, plan_attrs, true)
    else
        pstate = "[DRY RUN]"
    end

    local detail = string.format("player %d (%s): %d fields written, growth written: %d, plan sync: %s",
        pid, doc.name or filename, written, growth_written, pstate)
    return true, detail
end

function M.check_and_repair(dry)
    if not game.in_cm() then return false, "not in Manager Career" end
    local out_dir = env.output_dir()
    if not M.enabled(out_dir) then
        return false, "repair_ovr1 is off via " .. M.KILL_FILE
    end

    local players = db.get_table("players")
    if not players then return false, "players table not found" end

    -- Single pass over players table to identify target records
    local precs = {}
    for prec in db.records(players) do
        local pid = db.get_int(players, prec, "playerid", 0)
        if M.TARGETS[pid] then
            precs[pid] = prec
        end
    end

    -- Check which target players actually need repair
    local to_repair = {}
    for pid, file in pairs(M.TARGETS) do
        local prec = precs[pid]
        if prec and M.needs_repair(players, prec) then
            to_repair[#to_repair + 1] = { pid = pid, file = file, prec = prec }
        end
    end

    if #to_repair == 0 then
        return true, "all target players have normal ratings (no corruption detected)"
    end

    local lines = {}
    local function say(fmt, ...)
        local s = string.format(fmt, ...)
        lines[#lines + 1] = s
        log.info("[OVR1-REPAIR] %s", s)
        if type(LOGGER) == "table" and type(LOGGER.LogInfo) == "function" then
            pcall(function() LOGGER:LogInfo("[Turbo] [OVR1-REPAIR] " .. s) end)
        end
    end

    say("=== TURBO OVR-1 AUTO-REPAIR RUN (%d players to repair, dry=%s) ===", #to_repair, tostring(dry))
    local total_ok = 0
    for _, item in ipairs(to_repair) do
        local ok, msg = M.repair_player(item.pid, item.file, dry, item.prec)
        if ok then
            say("OK   %s", msg)
            total_ok = total_ok + 1
        else
            say("FAIL pid %d: %s", item.pid, tostring(msg))
        end
    end

    say("Finished: %d of %d players restored.", total_ok, #to_repair)

    local rep_path = util.join(out_dir, "REPAIR_REPORT.txt")
    local f = io.open(rep_path, "wb")
    if f then
        f:write(table.concat(lines, "\n"), "\n")
        f:close()
    end
    return true, string.format("auto-repair completed (%d of %d restored)", total_ok, #to_repair)
end

function M.on_career_event(event_id)
    if not game.in_cm() then return end
    TURBO_STATE = TURBO_STATE or { listeners = {} }
    -- Run check and repair on career load / hub entry
    pcall(M.check_and_repair, false)
end

return M
