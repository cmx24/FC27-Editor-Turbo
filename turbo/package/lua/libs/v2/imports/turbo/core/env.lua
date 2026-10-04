-- FC 27 LE Turbo - environment: Live Editor root folder, output folder, available natives

local util = require 'imports/turbo/core/util'

local M = {}

local CONFIG_FILE = "turbo_config.json"
local OUTPUT_DIR = "turbo_output"

-- Strip "...\lua\libs\..." (any slash style, any case) from a path and return the part before it.
local function root_from_lua_path(p)
    if type(p) ~= "string" or p == "" then return nil end
    p = p:gsub("^@", "")
    local lower = p:lower():gsub("/", "\\")
    local idx = lower:find("\\lua\\libs\\", 1, true)
    if not idx then return nil end
    local root = p:sub(1, idx - 1)
    if root == "" then return nil end
    return root
end

-- Candidate Live Editor root folders, most reliable first.
function M.candidate_roots()
    local out, seen = {}, {}
    local function add(r)
        if type(r) == "string" and r ~= "" then
            r = r:gsub("[\\/]+$", "")
            if r ~= "" and not seen[r:lower()] then
                seen[r:lower()] = true
                out[#out + 1] = r
            end
        end
    end

    -- 1) Where this file was loaded from
    local info = debug and debug.getinfo and debug.getinfo(1, "S")
    if info and info.source then add(root_from_lua_path(info.source)) end

    -- 2) package.path entries pointing at lua\libs
    if type(package) == "table" and type(package.path) == "string" then
        for entry in package.path:gmatch("[^;]+") do
            add(root_from_lua_path(entry))
        end
    end

    -- 3) LE_DATA_PATH global (set by Live Editor)
    if type(LE_DATA_PATH) == "string" then
        add(root_from_lua_path(LE_DATA_PATH) or LE_DATA_PATH)
    end

    -- 4) Default install folder (the offline test harness switches this off, so a real install on the same PC is not picked up)
    if not rawget(_G, "TURBO_NO_DEFAULT_ROOT") then add("C:\\FC 27 Live Editor") end

    return out
end

local cached_root = nil

-- Live Editor root = first candidate that contains turbo_config.json; else first candidate.
function M.le_root()
    if cached_root then return cached_root end
    local cands = M.candidate_roots()
    for _, r in ipairs(cands) do
        if util.file_exists(util.join(r, CONFIG_FILE)) then
            cached_root = r
            return r
        end
    end
    cached_root = cands[1]
    return cached_root
end

function M.reset_cache()
    cached_root = nil
end

function M.config_path()
    local root = M.le_root()
    if not root then return nil end
    return util.join(root, CONFIG_FILE)
end

-- Output folder. Uses <LE root>\turbo_output when it is writable, else the LE root,
-- else the Desktop. override = optional path from config.
function M.output_dir(override)
    local candidates = {}
    if type(override) == "string" and override ~= "" then candidates[#candidates + 1] = override end
    local root = M.le_root()
    if root then
        candidates[#candidates + 1] = util.join(root, OUTPUT_DIR)
        candidates[#candidates + 1] = root
    end
    local profile = os.getenv and os.getenv("USERPROFILE")
    if profile and profile ~= "" then
        candidates[#candidates + 1] = util.join(profile, "Desktop")
    end

    for _, dir in ipairs(candidates) do
        local probe = util.join(dir, ".turbo_write_test")
        local ok = util.write_file(probe, "ok")
        if ok then
            os.remove(probe)
            return dir
        end
    end
    return nil
end

-- Native/global function presence report
M.WATCHED_FUNCTIONS = {
    -- core natives
    "Log", "MessageBox", "IsInCM", "GetPlugin", "GetDBMeta", "GetSaveUID",
    "ReadBytes", "ReadShort", "ReadInteger", "ReadQword", "ReadFloat", "ReadString",
    "WriteBytes", "WriteShort", "WriteInteger", "WriteQword", "WriteFloat", "WriteString",
    "AOBScan", "AllocateMemory", "DeallocateMemory", "WriteJMP",
    "AddEventHandler", "GetEventHandlers", "RemoveEventHandler", "ClearEventHandlersForEvent",
    -- API v1
    "GetCurrentDate", "GetUserTeamID", "GetTeamIdFromPlayerId", "GetTeamName", "GetPlayerName",
    "GetCompetitionNameByID", "GetCompetitionNameByObjID",
    "SetPlayerMorale", "SetPlayerForm", "SetPlayerFitness",
    "PlayerHasDevelopementPlan", "PlayerSetValueInDevelopementPlan",
    "GetPlayersStats", "GetPlayerStats", "DeletePlayer", "CreatePlayer", "PlayerExists",
    "TransferPlayer", "LoanPlayer", "ReleasePlayerFromTeam", "TerminateLoan", "DeletePresignedContract",
    "IsPlayerLoanedOut", "IsPlayerPresigned", "IsPlayerTransferListed", "IsPlayerLoanListed",
    "AddPlayerToTransferList", "AddPlayerToLoanList", "RemovePlayerFromLists",
    "RemovePlayerFromTransferList", "RemovePlayerFromLoanList",
    "GetDBTablesNames", "GetDBTableFields", "GetDBTableRows", "InsertDBTableRow", "EditDBTableField",
    "GetUserTransferBudget", "SetUserTransferBudget", "GetCPUTransferBudget", "SetCPUTransferBudget",
    -- natives behind the v1/v2 Lua wrappers
    "cTransferPlayer", "cLoanPlayer", "cReleasePlayer", "cIsPlayerTransferListed", "cIsPlayerLoanListed",
    "cAddPlayerToTransferList", "cAddPlayerToLoanList", "cRemovePlayerFromLists",
    "cRemovePlayerFromTransferList", "cRemovePlayerFromLoanList",
    "cAddTransferBan", "cRemoveTransferBan", "cGetTransferBans", "cSaveTransferBans",
    "PlayerDevelopmentManagerLoad", "PlayerDevelopmentManagerSave",
    "PlayerDevelopmentManagerAddPlayer", "PlayerDevelopmentManagerRemovePlayer",
    "GameplayAttribulatorSetVar", "GameplayAttribulatorGetVarType",
    "AardvarkGetInt", "AardvarkSetInt", "SetGameLocString", "GetGameLocString",
    "LegacyFileExist", "LegacyFileExport", "SendHTTPRequest",
}

function M.has(name)
    return type(_G[name]) == "function"
end

-- Is a Live Editor API function really usable?
-- FC 27 LE v27.1.2 keeps the FC 26 Lua API names, but several of them are only Lua wrappers around natives that build
-- does not have (TransferPlayer calls cTransferPlayer, which is missing) or placeholders that print "TODO: FC27" /
-- "deprecated" and do nothing (SetSquadRole, GetTransferBudget). Calling those either raises "attempt to call a nil
-- value" or silently does nothing, so Turbo checks the function first:
--   * not a function                        -> missing
--   * a Lua function from Live Editor's own lua\libs folder: its source lines are read; a placeholder (TODO / NOT
--     IMPLEMENTED / deprecated) or a call to a c<Name> native that does not exist makes it unavailable
-- Returns fn or nil, reason. Results are cached per function value.
local api_cache = setmetatable({}, { __mode = "k" })

local function source_lines(path, first, last)
    local f = io.open(path, "r")
    if not f then return nil end
    local out, n = {}, 0
    for line in f:lines() do
        n = n + 1
        if n >= first then out[#out + 1] = line end
        if n >= last then break end
    end
    f:close()
    return table.concat(out, "\n")
end

local function inspect_lua(name, fn)
    if type(debug) ~= "table" or type(debug.getinfo) ~= "function" then return true end
    local ok, info = pcall(debug.getinfo, fn, "S")
    if not ok or type(info) ~= "table" or info.what ~= "Lua" then return true end
    local src = type(info.source) == "string" and info.source or ""
    if src:sub(1, 1) ~= "@" then return true end
    local path = src:sub(2)
    local lower = path:lower():gsub("/", "\\")
    -- only Live Editor's own libraries; Turbo's files and anything else are taken as they are
    if not lower:find("\\lua\\libs\\", 1, true) or lower:find("\\imports\\turbo\\", 1, true) then return true end
    local body = source_lines(path, info.linedefined or 0, info.lastlinedefined or 0)
    if not body then return true end
    local code = body:gsub("%-%-[^\n]*", "")   -- drop comments
    if code:find("NOT IMPLEMENTED", 1, true) or code:find("TODO", 1, true) or code:lower():find("deprecated", 1, true) then
        return false, string.format("%s is a placeholder in this Live Editor build (it does nothing yet)", name)
    end
    for native in code:gmatch("[^%w_.:](c[A-Z][%w_]*)%s*%(") do
        if type(_G[native]) ~= "function" then
            return false, string.format("%s is not available in this Live Editor build (it needs the native %s, which "
                .. "this Live Editor build does not have)", name, native)
        end
    end
    return true
end

function M.api(name)
    local fn = _G[name]
    if type(fn) ~= "function" then return nil, name .. " is not available in this Live Editor build" end
    local cached = api_cache[fn]
    if cached == nil then
        local ok, reason = inspect_lua(name, fn)
        cached = ok and true or (reason or (name .. " is not available in this Live Editor build"))
        api_cache[fn] = cached
    end
    if cached == true then return fn end
    return nil, cached
end

-- Forget every verdict: natives defined later (Turbo.dll's game calls define the c-natives Live Editor v27.1.2 lacks)
-- make wrappers usable that were not
function M.reset_api_cache()
    for k in pairs(api_cache) do api_cache[k] = nil end
end

-- Messages that mean "this Live Editor build cannot do it" (not a Turbo failure)
function M.is_unavailable_message(msg)
    msg = tostring(msg or "")
    return msg:find("not available in this Live Editor build", 1, true) ~= nil
        or msg:find("is a placeholder in this Live Editor build", 1, true) ~= nil
end

function M.functions_report()
    local present, missing = {}, {}
    for _, name in ipairs(M.WATCHED_FUNCTIONS) do
        if M.has(name) then present[#present + 1] = name else missing[#missing + 1] = name end
    end
    return present, missing
end

-- Fingerprint of the running game + LE, used to key calibration data.
function M.build_key()
    local size = LE_GAME_MODULE_SIZE
    if type(size) ~= "number" then size = 0 end
    return string.format("%s|%X", tostring(LE_VERSION or "unknown"), math.tointeger(size) or 0)
end

return M
