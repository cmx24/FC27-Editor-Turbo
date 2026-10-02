-- Turbo feature: probe. Read-only. Writes <output>\turbo_probe_<date>.txt with everything Turbo
-- needs to know about this Live Editor + game build, and checks each memory-calibrated feature.

local util = require 'imports/turbo/core/util'
local env = require 'imports/turbo/core/env'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local events = require 'imports/turbo/core/events'
local version = require 'imports/turbo/core/version'

local M = {}

M.KEY_TABLES = {
    "players", "teams", "teamplayerlinks", "cm_teamsheets", "career_playercontract", "career_users",
    "playerloans", "formations", "manager", "leagueteamlinks",
}

M.KEY_MANAGERS = {
    "FCEGameModesFCECareerModeUserManager", "FCEGameModesFCECareerModePlayerStatusManager",
    "FCEGameModesFCECareerModeTransferManager", "FCEGameModesFCECareerModeCalendarManager",
}

M.KEY_EVENTS = { "DAY_PASSED", "ABOUT_TO_ENTER_PREMATCH", "POST_LOAD_PREPARE", "USER_MATCH_COMPLETED" }

local function all_table_names()
    if type(GetDBTablesNames) == "function" then
        local ok, names = pcall(GetDBTablesNames)
        if ok and type(names) == "table" and #names > 0 then
            local out = {}
            for _, n in ipairs(names) do out[#out + 1] = tostring(n) end
            table.sort(out)
            return out, "GetDBTablesNames"
        end
    end
    if db.available() and type(LE.db.Reset) == "function" then
        local ok = pcall(function() LE.db:Reset() end)
        if ok and type(LE.db.tables) == "table" then
            return util.sorted_keys(LE.db.tables), "LE.db"
        end
    end
    return {}, "unavailable"
end

function M.collect()
    local r = {}
    local function add(fmt, ...) r[#r + 1] = string.format(fmt, ...) end

    add("%s %s probe - %s", version.name, version.version, os.date("%Y-%m-%d %H:%M:%S"))
    add("LE_VERSION: %s", tostring(LE_VERSION))
    add("Game module: %s base=0x%X size=0x%X", tostring(LE_GAME_MODULE_NAME),
        math.tointeger(LE_GAME_MODULE_BASE or 0) or 0, math.tointeger(LE_GAME_MODULE_SIZE or 0) or 0)
    add("Build key: %s", env.build_key())
    add("LE root: %s", tostring(env.le_root()))
    add("Config: %s (%s)", tostring(env.config_path()), util.file_exists(env.config_path() or "") and "found" or "MISSING")
    add("Root candidates: %s", table.concat(env.candidate_roots(), " | "))
    add("")

    local in_cm = game.in_cm()
    add("In career mode: %s", tostring(in_cm))
    if in_cm then
        local tid = game.user_team_id()
        add("User team: %d (%s)", tid, game.team_name(tid))
        local _, count, source = game.user_squad()
        add("User squad: %d players from %s", count, tostring(source))
        local d = game.current_date()
        add("Current date: %s", d and string.format("%04d-%02d-%02d via %s", d.year, d.month, d.day, d.source) or "unavailable")
    end
    add("")

    local present, missing = env.functions_report()
    add("Functions present (%d): %s", #present, table.concat(present, ", "))
    add("Functions missing (%d): %s", #missing, table.concat(missing, ", "))
    add("")

    for _, name in ipairs(M.KEY_EVENTS) do
        add("Event %s -> %s (FC27 name %s, FC26 name %s)", name, tostring(events.resolve(name)),
            tostring(_G["ENUM_FCEGameModesCM_EVENT_MSG_" .. name]), tostring(_G["ENUM_CM_EVENT_MSG_" .. name]))
    end
    add("")

    local names, src = all_table_names()
    add("DB tables (%d via %s): %s", #names, src, table.concat(names, ", "))
    for _, t in ipairs(M.KEY_TABLES) do
        local tbl, err = db.get_table(t)
        if tbl then
            local fields = {}
            for _, f in ipairs(db.field_names(tbl)) do
                local info = db.field_info(tbl, f)
                if info.type == "int" and info.max then
                    fields[#fields + 1] = string.format("%s[%d..%d]", f, info.min, info.max)
                elseif info.type == "string" then
                    fields[#fields + 1] = string.format("%s[str%d]", f, info.max_len or 0)
                else
                    fields[#fields + 1] = string.format("%s[%s]", f, info.type)
                end
            end
            add("TABLE %s rows=%s fields(%d): %s", t, tostring(tbl.written_records), #fields, table.concat(fields, " "))
        else
            add("TABLE %s: %s", t, tostring(err))
        end
    end
    add("")

    pcall(require, 'imports/career_mode/enums')
    for _, m in ipairs(M.KEY_MANAGERS) do
        local id = _G["ENUM_" .. m]
        local addr = (type(id) == "number" and in_cm) and mem.manager(id) or nil
        add("Manager %s (type %s): %s", m, tostring(id), addr and string.format("0x%X", addr) or "not available")
    end
    add("")

    if in_cm then
        local squad, count = game.user_squad()
        local okr, sr = pcall(require, 'imports/turbo/features/squad_role')
        if okr then
            local layout, err = sr.locate(squad, count, nil)
            add("squad_role memory: %s", layout and string.format("vector at PlayerStatusManager+0x%X, entry %d bytes, %d/%d squad matches",
                layout.offset, layout.size, layout.matched, count) or tostring(err))
        end
        local okf, fx = pcall(require, 'imports/turbo/features/export_fixtures')
        if okf then
            local found, err = fx.locate(game.team_ids(), nil)
            add("export_fixtures memory: %s", found and string.format("fixtures +0x%X (%d), standings +0x%X",
                found.fixtures_off, #found.fixtures, found.standings_off) or tostring(err))
        end
        local okt, th = pcall(require, 'imports/turbo/features/export_transfer_history')
        if okt then
            local storage, off = th.locate(game.player_ids(), nil)
            add("export_transfer_history memory: %s", storage and string.format("storage at TransferManager+0x%X", off) or tostring(off))
        end
    else
        add("Memory checks skipped: load a career save and run the probe again.")
    end
    return r
end

function M.run(ctx)
    local lines = M.collect()
    local log = require 'imports/turbo/core/log'
    for _, l in ipairs(lines) do
        if l ~= "" then log.info("%s", l) end
    end
    if not ctx.out_dir then return true, "probe written to the log only (no writable output folder)" end
    local path = util.join(ctx.out_dir, "turbo_probe_" .. os.date("%Y_%m_%d_%H%M%S") .. ".txt")
    local ok, err = util.write_file(path, table.concat(lines, "\r\n") .. "\r\n")
    if not ok then return true, "probe written to the log; file failed: " .. tostring(err) end
    return true, "probe saved to " .. path
end

return M
