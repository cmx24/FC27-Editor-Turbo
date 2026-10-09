-- FC 27 LE Turbo: REPAIR script for corrupted players in Torino Career.
-- Targets:
--   199254 João Pedro (OVR 76, POT 86)
--   216145 Armando Izzo (OVR 70, POT 76)
--   220491 Emmanuel Gyasi (OVR 72, POT 76)
--   220493 Antonio Barreca (OVR 76, POT 82)
--   243497 Sebastian Walukiewicz (OVR 76, POT 84)
--   244836 Jacopo Segre (OVR 73, POT 78)
--
-- This script:
-- 1. Loads the original clean player presets from turbo_output\players\<name>_<pid>.json.
-- 2. Restores all 34 core attributes + profile fields (overallrating, potential) in the `players` table.
-- 3. Restores career_playergrowthuserseason (DyYs) record if present (overall + 34 core attributes).
-- 4. Synchronizes all attributes unconditionally into the game's native development plan via PlayerSetValueInDevelopementPlan.
-- 5. Clears modifier to 0 in players table.
-- 6. Writes a detailed report to turbo_output\REPAIR_REPORT.txt.

local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local env = require 'imports/turbo/core/env'
local log = require 'imports/turbo/core/log'
local util = require 'imports/turbo/core/util'
local moves = require 'imports/turbo/core/moves'
local preset = require 'imports/turbo/core/preset'

local TARGETS = {
    [220493] = "Antonio_Barreca_220493.json",
    [199254] = "Jo_o_Pedro_199254.json",
    [216145] = "Armando_Izzo_216145.json",
    [220491] = "Emmanuel_Gyasi_220491.json",
    [243497] = "Sebastian_Walukiewicz_243497.json",
    [244836] = "Jacopo_Segre_244836.json",
}

local function repair_one(pid, filename, dry, prec_override)
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

    -- Clear modifier to 0 so unearned modifiers do not distort ratings
    if not dry and db.has_field(players, "modifier") then
        pcall(db.set, players, prec, "modifier", 0)
    end

    -- Update career_playergrowthuserseason table (DyYs) if present
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

    return true, string.format("player %d (%s): %d fields written, growth written: %d, plan sync: %s",
        pid, doc.name or filename, written, growth_written, pstate)
end

local function main(target_only, dry)
    if not game.in_cm() then return false, "load a Manager Career first" end
    local lines = {}
    local function say(fmt, ...)
        local s = string.format(fmt, ...)
        lines[#lines + 1] = s
        log.info("[REPAIR] %s", s)
        if type(LOGGER) == "table" and type(LOGGER.LogInfo) == "function" then
            pcall(function() LOGGER:LogInfo("[Turbo] [REPAIR] " .. s) end)
        end
    end

    say("=== TURBO OVR-1 REPAIR RUN (target=%s, dry=%s) ===", tostring(target_only or "all"), tostring(dry))
    local pids = target_only and { target_only } or { 220493, 199254, 216145, 220491, 243497, 244836 }
    local total_ok = 0

    local players = db.get_table("players")
    local precs = {}
    if players then
        for prec in db.records(players) do
            local pid = db.get_int(players, prec, "playerid", 0)
            if TARGETS[pid] then precs[pid] = prec end
        end
    end

    for _, pid in ipairs(pids) do
        local file = TARGETS[pid]
        if file then
            local ok, msg = repair_one(pid, file, dry, precs[pid])
            if ok then
                say("OK   %s", msg)
                total_ok = total_ok + 1
            else
                say("FAIL pid %d: %s", pid, tostring(msg))
            end
        else
            say("SKIP pid %d (no target file)", pid)
        end
    end

    say("Finished: %d of %d players repaired.", total_ok, #pids)

    local rep_path = util.join(env.output_dir(), "REPAIR_REPORT.txt")
    local f = io.open(rep_path, "wb")
    if f then
        f:write(table.concat(lines, "\n"), "\n")
        f:close()
    end
    return true, string.format("repair completed (%d of %d ok), report written to %s", total_ok, #pids, rep_path)
end

-- Export function for Live Editor / Lua Engine execution:
--   dofile("C:/FC 27 Live Editor/lua/scripts/turbo_repair_ovr1.lua")
--   repair_ovr1(220493) -- to repair only Barreca
--   repair_ovr1()       -- to repair all 6
_G["repair_ovr1"] = main

local ok, res = pcall(main, nil, false)
if not ok then
    log.warn("repair_ovr1 script run error: %s", tostring(res))
    if type(MessageBox) == "function" then
        pcall(MessageBox, "Turbo OVR1 Repair Error", tostring(res):gsub("%%", "%%%%"))
    end
else
    log.info("%s", tostring(res))
    if type(MessageBox) == "function" then
        pcall(MessageBox, "Turbo OVR1 Repair", tostring(res):gsub("%%", "%%%%"))
    end
end
