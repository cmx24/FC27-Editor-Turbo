-- FC 27 LE Turbo - career-mode context: user team, squad, date, names

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'

local M = {}

-- Keep a reference to the native (C) GetCurrentDate before any Lua library replaces it.
-- lua\libs\v2\imports\other\helpers.lua overrides the global with a version that reads
-- fixed calendar-manager offsets carried over from FC 26.
local native_get_current_date = nil
if type(GetCurrentDate) == "function" and debug and debug.getinfo then
    local info = debug.getinfo(GetCurrentDate, "S")
    if info and info.what == "C" then native_get_current_date = GetCurrentDate end
end
M._native_get_current_date = native_get_current_date

function M.in_cm()
    if type(IsInCM) ~= "function" then return false end
    local ok, res = pcall(IsInCM)
    return ok and res == true
end

local helpers_loaded = false
local function load_cm_helpers()
    if helpers_loaded then return true end
    local ok, err = pcall(require, 'imports/career_mode/helpers')
    if not ok then
        log.warn("cannot load career_mode/helpers: %s", tostring(err))
        return false
    end
    helpers_loaded = true
    return true
end

-- Native v1 GetUserTeamID, captured before career_mode/helpers replaces the global
local native_get_user_team_id = nil
if type(GetUserTeamID) == "function" and debug and debug.getinfo then
    local info = debug.getinfo(GetUserTeamID, "S")
    if info and info.what == "C" then native_get_user_team_id = GetUserTeamID end
end

-- User club team id in career mode, 0 when unknown.
-- 1) FCE CareerMode UserManager -> user info (+0x18) -> club team id (+0x1F4), the layout
--    FC 27 Live Editor's own career_mode/helpers.lua uses; every hop is pointer-checked.
-- 2) Native v1 GetUserTeamID.
function M.user_team_id()
    if not M.in_cm() then return 0 end
    load_cm_helpers()
    if type(ENUM_FCEGameModesFCECareerModeUserManager) == "number" then
        local mem = require 'imports/turbo/core/mem'
        local user_mgr = mem.manager(ENUM_FCEGameModesFCECareerModeUserManager)
        local user_info = user_mgr and mem.ptr(user_mgr + 0x18)
        local tid = user_info and mem.int(user_info + 0x1F4)
        if tid and tid > 0 then return tid end
    end
    if native_get_user_team_id then
        local ok, id = pcall(native_get_user_team_id)
        id = ok and util.to_int(id) or nil
        if id and id > 0 then return id end
    end
    return 0
end

-- Player IDs of the user's senior squad.
-- Primary source: cm_teamsheets (what FC 27's own helpers use); fallback: teamplayerlinks.
-- Returns set {[playerid]=true}, count, source
function M.user_squad()
    local result, count = {}, 0
    if not M.in_cm() then return result, 0, "not in career mode" end
    local teamid = M.user_team_id()
    if teamid <= 0 then return result, 0, "user team not found" end

    local sheets = db.get_table("cm_teamsheets")
    if sheets and db.has_fields(sheets, { "teamid", "playerid1" }) then
        for rec in db.records(sheets) do
            if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
                for i = 1, 52 do
                    local fname = "playerid" .. i
                    if not db.has_field(sheets, fname) then break end
                    local pid = sheets:GetRecordFieldValue(rec, fname)
                    if pid == nil or pid == -1 then break end
                    if pid > 0 and not result[pid] then
                        result[pid] = true
                        count = count + 1
                    end
                end
                break
            end
        end
        if count > 0 then return result, count, "cm_teamsheets" end
    end

    local links = db.get_table("teamplayerlinks")
    if links and db.has_fields(links, { "teamid", "playerid" }) then
        for rec in db.records(links) do
            if links:GetRecordFieldValue(rec, "teamid") == teamid then
                local pid = links:GetRecordFieldValue(rec, "playerid")
                if pid and pid > 0 and not result[pid] then
                    result[pid] = true
                    count = count + 1
                end
            end
        end
        if count > 0 then return result, count, "teamplayerlinks" end
    end

    return result, 0, "no squad rows found for team " .. teamid
end

local function valid_date(d)
    return type(d) == "table"
        and util.is_int(d.day) and util.is_int(d.month) and util.is_int(d.year)
        and d.year >= 2000 and d.year <= 2200
        and d.month >= 1 and d.month <= 12
        and d.day >= 1 and d.day <= 31
end

-- Current in-game date as {day, month, year, int=YYYYMMDD, source}. nil when unavailable.
function M.current_date()
    local tries = {}
    if native_get_current_date then tries[#tries + 1] = { "native", native_get_current_date } end
    if type(GetCurrentDate) == "function" and GetCurrentDate ~= native_get_current_date then
        tries[#tries + 1] = { "lua", GetCurrentDate }
    end
    for _, t in ipairs(tries) do
        local ok, d = pcall(t[2])
        if ok and valid_date(d) then
            local day, month, year = math.tointeger(d.day), math.tointeger(d.month), math.tointeger(d.year)
            return { day = day, month = month, year = year, int = year * 10000 + month * 100 + day, source = t[1] }
        end
    end
    return nil
end

function M.player_name(pid)
    if type(GetPlayerName) ~= "function" then return tostring(pid) end
    local ok, name = pcall(GetPlayerName, pid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return tostring(pid)
end

function M.team_name(tid)
    if type(GetTeamName) ~= "function" then return tostring(tid) end
    local ok, name = pcall(GetTeamName, tid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return tostring(tid)
end

function M.team_of_player(pid)
    if type(GetTeamIdFromPlayerId) ~= "function" then return 0 end
    local ok, tid = pcall(GetTeamIdFromPlayerId, pid)
    tid = ok and util.to_int(tid) or 0
    return tid or 0
end

function M.competition_name(compobjid)
    if type(GetCompetitionNameByObjID) ~= "function" then return tostring(compobjid) end
    local ok, name = pcall(GetCompetitionNameByObjID, compobjid)
    if ok and type(name) == "string" and name ~= "" then return name end
    return tostring(compobjid)
end

-- Set of all team IDs in the teams table (used to sanity-check memory reads)
function M.team_ids()
    local set = {}
    local teams = db.get_table("teams")
    if not teams or not db.has_field(teams, "teamid") then return set end
    for rec in db.records(teams) do
        local tid = teams:GetRecordFieldValue(rec, "teamid")
        if tid then set[tid] = true end
    end
    return set
end

-- Set of all player IDs in the players table
function M.player_ids()
    local set = {}
    local players = db.get_table("players")
    if not players or not db.has_field(players, "playerid") then return set end
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if pid then set[pid] = true end
    end
    return set
end

return M
