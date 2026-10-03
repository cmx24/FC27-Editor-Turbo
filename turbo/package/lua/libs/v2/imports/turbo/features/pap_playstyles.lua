-- Turbo feature: give your own player every playstyle in Player Career and re-apply after resets.
-- Port of FC 26 pap_all_playstyles.lua (user-manager offsets come from FC 27 LE's updated library).
-- Config: auto.pap_playstyles { enabled, playstyles1 ("max" or bitmask), playstyles2, events }

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local log = require 'imports/turbo/core/log'

local M = {}

local FIELDS = { "playerid", "trait1", "icontrait1", "trait2", "icontrait2" }

local function user_manager()
    local ok, cls = pcall(require, 'imports/career_mode/FCECareerModeUserManager')
    if not ok or type(cls) ~= "table" then return nil, "FCECareerModeUserManager library not available" end
    local mgr = cls:new()
    local mem = require 'imports/turbo/core/mem'
    if not mem.is_ptr(mgr:GetAddr(), 8) then return nil, "career user manager not found" end
    return mgr
end

local function value_for(tbl, field, v)
    if v == "max" then
        local info = db.field_info(tbl, field)
        if not info or not info.max then return nil, "cannot compute max for " .. field end
        return info.max
    end
    local iv = util.to_int(v)
    if iv == nil then return nil, field .. " value must be an integer or \"max\"" end
    return iv
end

local function apply(ctx)
    local mgr, merr = user_manager()
    if not mgr then return false, merr end
    if not mgr:IsPlayerCareer() then return false, "not in Player Career (Play As Player)" end

    local pid = util.to_int(mgr:GetPAPID())
    if not pid or pid <= 0 then return false, "your player id was not found" end

    local players, terr = db.get_table("players")
    if not players then return false, terr end
    local ok, missing = db.has_fields(players, FIELDS)
    if not ok then return false, "players table lacks fields: " .. table.concat(missing, ", ") end

    local v1, e1 = value_for(players, "trait1", ctx.cfg.playstyles1)
    if not v1 then return false, e1 end
    local v2, e2 = value_for(players, "trait2", ctx.cfg.playstyles2)
    if not v2 then return false, e2 end

    local rec = db.find(players, "playerid", pid)
    if not rec then return false, string.format("player %d not in players table", pid) end

    local writes = {
        { "trait1", v1 }, { "icontrait1", v1 }, { "trait2", v2 }, { "icontrait2", v2 },
    }
    for _, w in ipairs(writes) do
        local wok, werr = db.set(players, rec, w[1], w[2], ctx.dry)
        if not wok then return false, werr end
    end
    return true, string.format("player %d: trait1/icontrait1=%d, trait2/icontrait2=%d", pid, v1, v2)
end

function M.run(ctx)
    return apply(ctx)
end

function M.auto(ctx, event_id)
    local ok, summary = apply(ctx)
    if ok then
        log.debug("pap_playstyles applied on event %s: %s", tostring(event_id), summary)
    elseif event_id ~= -1 then
        log.debug("pap_playstyles skipped on event %s: %s", tostring(event_id), summary)
    end
end

return M
