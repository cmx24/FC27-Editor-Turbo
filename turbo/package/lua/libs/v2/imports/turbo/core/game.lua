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
    local src = info and tostring(info.source) or ""
    if info and (info.what == "C" or not src:find("other/helpers", 1, true)) then native_get_current_date = GetCurrentDate end
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
    -- anything but Live Editor's Lua replacement (its unchecked memory walk crashes outside a career)
    local info = debug.getinfo(GetUserTeamID, "S")
    local src = info and tostring(info.source) or ""
    if info and (info.what == "C" or not src:find("career_mode", 1, true)) then native_get_user_team_id = GetUserTeamID end
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
-- The sheet is read over ALL slots playerid0..52: a -1 / nil slot is skipped, never the end of the list (a gap before the
-- reserves used to drop every player after it). When teamplayerlinks lists more players than the sheet, the squad is the
-- union of both and the difference is logged (docs/TURBO_2_0_PLAN.md section 2, fix 2).
-- Returns set {[playerid]=true}, count, source, info {sheet = n, links = n, gaps = n}
function M.user_squad()
    local result = {}
    local info = { sheet = 0, links = 0, gaps = 0 }
    if not M.in_cm() then return result, 0, "not in career mode", info end
    local teamid = M.user_team_id()
    if teamid <= 0 then return result, 0, "user team not found", info end

    local sheet_set, sheet_n, gaps = {}, 0, 0
    local sheets = db.get_table("cm_teamsheets")
    if sheets and db.has_fields(sheets, { "teamid", "playerid1" }) then
        for rec in db.records(sheets) do
            if sheets:GetRecordFieldValue(rec, "teamid") == teamid then
                -- playerid0 is the first starter (seen in FC 27: the goalkeeper); FC 27 LE's own helper starts at 1
                local pending = 0   -- empty slots seen since the last filled one: a gap only when a player follows
                for i = 0, 52 do
                    local fname = "playerid" .. i
                    if db.has_field(sheets, fname) then
                        local pid = sheets:GetRecordFieldValue(rec, fname)
                        if pid and pid > 0 then
                            gaps = gaps + pending
                            pending = 0
                            if not sheet_set[pid] then
                                sheet_set[pid] = true
                                sheet_n = sheet_n + 1
                            end
                        elseif sheet_n > 0 then
                            pending = pending + 1
                        end
                    end
                end
                break
            end
        end
    end

    local links_set, links_n = {}, 0
    local links = db.get_table("teamplayerlinks")
    if links and db.has_fields(links, { "teamid", "playerid" }) then
        for rec in db.records(links) do
            if links:GetRecordFieldValue(rec, "teamid") == teamid then
                local pid = links:GetRecordFieldValue(rec, "playerid")
                if pid and pid > 0 and not links_set[pid] then
                    links_set[pid] = true
                    links_n = links_n + 1
                end
            end
        end
    end
    info.sheet, info.links, info.gaps = sheet_n, links_n, gaps

    if sheet_n > 0 and sheet_n >= links_n then return sheet_set, sheet_n, "cm_teamsheets", info end
    if links_n > 0 then
        for pid in pairs(sheet_set) do links_set[pid] = true end
        local count = 0
        for _ in pairs(links_set) do count = count + 1 end
        if sheet_n > 0 then
            log.info("user_squad: cm_teamsheets lists %d players (%d empty slots between players), teamplayerlinks %d: using both (%d)",
                sheet_n, gaps, links_n, count)
        end
        return links_set, count, "teamplayerlinks", info
    end
    return result, 0, "no squad rows found for team " .. teamid, info
end

-- day, month, year of a date object (Lua table or userdata), or nil when it is not a plausible in-game date
local function date_parts(d)
    local day, month, year = util.index(d, "day"), util.index(d, "month"), util.index(d, "year")
    if not (util.is_int(day) and util.is_int(month) and util.is_int(year)) then return nil end
    if year < 2000 or year > 2200 or month < 1 or month > 12 or day < 1 or day > 31 then return nil end
    return math.tointeger(day), math.tointeger(month), math.tointeger(year)
end

-- Current in-game date as {day, month, year, int=YYYYMMDD, source}. nil when unavailable.
-- The in-game date from the career CalendarManager, read only through the Turbo GUI's memory map (Live Editor's own Lua
-- GetCurrentDate in other/helpers.lua reads the same place unchecked: outside a career, or if the manager is missing,
-- that read crashes the game). FC 26 kept day/month/year at +0x34/+0x38/+0x3C; if FC 27 moved them, the first
-- plausible day/month/year triple in the manager is used and its offset is reported.
local function calendar_date()
    local mem = require 'imports/turbo/core/mem'
    pcall(require, 'imports/career_mode/enums')
    local id = _G["ENUM_FCEGameModesFCECareerModeCalendarManager"]
    if type(id) ~= "number" or not mem.map_available() then return nil end
    local cal = mem.manager(id)
    if not cal then return nil end
    local function triple(off)
        local d, m, y = mem.int(cal + off), mem.int(cal + off + 4), mem.int(cal + off + 8)
        if d and m and y and d >= 1 and d <= 31 and m >= 1 and m <= 12 and y >= 2020 and y <= 2100 then
            return { day = d, month = m, year = y }
        end
        return nil
    end
    local d = triple(0x34)
    if d then d.offset = 0x34 return d end
    for off = 0x08, 0x200, 4 do
        d = triple(off)
        if d then d.offset = off return d end
    end
    return nil
end

M._calendar_date = calendar_date

function M.current_date()
    local tries = {}
    if native_get_current_date then tries[#tries + 1] = { "native", native_get_current_date } end
    tries[#tries + 1] = { "calendar", calendar_date }
    for _, t in ipairs(tries) do
        local ok, d = pcall(t[2])
        local day, month, year
        if ok then day, month, year = date_parts(d) end
        if day then
            local src = t[1] .. ((type(d) == "table" and math.type(d.offset) == "integer") and string.format("+0x%X", d.offset) or "")
            return { day = day, month = month, year = year, int = year * 10000 + month * 100 + day, source = src }
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
    if type(GetTeamIdFromPlayerId) ~= "function" then
        -- FC 27 LE v27.1.2 has no GetTeamIdFromPlayerId: the club link in teamplayerlinks
        local okm, moves = pcall(require, 'imports/turbo/core/moves')
        return okm and moves.team_of_player(pid) or 0
    end
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
