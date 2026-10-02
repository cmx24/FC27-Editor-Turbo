-- Turbo feature: FC 27 head-model list.
-- FC 26 fix_players_headmodels.lua carried a hard-coded list of 5,771 player IDs built for FC 26 v1.6.1.
-- Turbo builds the FC 27 list from the game's own data instead:
--   mode "capture": on an unmodified database, record every player whose head asset is his own
--                   (hashighqualityhead = 1 and headassetid = playerid) into <output>\headmodels_fc27.json
--   mode "apply":   players in the list get their real head; with generic_for_others = true every
--                   other player gets a generic head (FC 26 behaviour)

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'

local M = {}

local FIELDS = { "playerid", "hashighqualityhead", "headclasscode", "headassetid" }

local function json()
    local ok, j = pcall(require, 'imports/external/json')
    if ok and type(j) == "table" then return j end
    return nil
end

local function list_path(ctx)
    local name = ctx.cfg.list_file
    if type(name) ~= "string" or name == "" or name:find("[\\/:]") then
        return nil, "list_file must be a plain file name such as headmodels_fc27.json"
    end
    if not ctx.out_dir then return nil, "no writable output folder" end
    return util.join(ctx.out_dir, name)
end

local function capture(ctx, players)
    local ids = {}
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if pid and pid > 0
            and players:GetRecordFieldValue(rec, "hashighqualityhead") == 1
            and players:GetRecordFieldValue(rec, "headassetid") == pid then
            ids[#ids + 1] = pid
        end
    end
    table.sort(ids)
    local path, perr = list_path(ctx)
    if not path then return false, perr end
    local j = json()
    if not j then return false, "json library not available" end
    if not ctx.dry then
        local ok, werr = util.write_file(path, j.encode(ids))
        if not ok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("captured %d players with their own head model into %s", #ids, path)
end

local function apply(ctx, players)
    local path, perr = list_path(ctx)
    if not path then return false, perr end
    if not util.file_exists(path) then return false, "list not found: " .. path .. " (run mode \"capture\" first)" end
    local j = json()
    if not j then return false, "json library not available" end
    local okd, data = pcall(j.decode, util.read_file(path) or "")
    if not okd or type(data) ~= "table" then return false, "list is not valid JSON: " .. path end
    local ids, bad = util.int_list(data)
    if bad > 0 then return false, string.format("list has %d non-integer entries", bad) end
    local min_size = util.to_int(ctx.cfg.min_list_size) or 100
    if #ids < min_size then
        return false, string.format("list has %d players, fewer than min_list_size %d; refusing to touch heads", #ids, min_size)
    end
    local valid = util.set_of(ids)
    local generic = ctx.cfg.generic_for_others == true

    local real, gen = 0, 0
    for rec in db.records(players) do
        local pid = players:GetRecordFieldValue(rec, "playerid")
        if pid and pid > 0 and valid[pid] then
            for _, w in ipairs({ { "hashighqualityhead", 1 }, { "headclasscode", 0 }, { "headassetid", pid } }) do
                local wok, werr = db.set(players, rec, w[1], w[2], ctx.dry)
                if not wok then return false, string.format("player %d: %s", pid, werr) end
            end
            real = real + 1
        elseif generic then
            for _, w in ipairs({ { "hashighqualityhead", 0 }, { "headclasscode", 1 } }) do
                local wok, werr = db.set(players, rec, w[1], w[2], ctx.dry)
                if not wok then return false, string.format("player %s: %s", tostring(pid), werr) end
            end
            gen = gen + 1
        end
    end
    return true, string.format("%d players set to their real head, %d set to a generic head", real, gen)
end

function M.run(ctx)
    local players, err = db.get_table("players")
    if not players then return false, err end
    local ok, missing = db.has_fields(players, FIELDS)
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    local mode = ctx.cfg.mode
    if mode == "capture" then return capture(ctx, players) end
    if mode == "apply" then return apply(ctx, players) end
    return false, "mode must be \"capture\" or \"apply\""
end

return M
