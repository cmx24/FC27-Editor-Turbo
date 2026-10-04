-- Turbo feature: youth academy helpers (Manager Career).
--   "youth": { "mode": "list" | "set", "playerid": 0, "potential": 0, "position": -1, "tier": -1, "variance": -1,
--              "confirm": false }
-- list: the academy (career_youthplayers rows) with each player's name, age group, position, overall, potential,
--       potential variance and tier, written to turbo_output\youth_academy.csv and summarised.
-- set : one academy player: potential (players.potential, 1..99), position (players.preferredposition1, 0..27), tier
--       (career_youthplayers.playertier) and the potential variance (career_youthplayers.potentialvariance: the width of
--       the potential range the academy screen shows; 0 = the scout report gives the exact potential).
-- Only players who are in the academy table are changed; every value is checked against the field's range first.

local util = require 'imports/turbo/core/util'
local db = require 'imports/turbo/core/db'
local game = require 'imports/turbo/core/game'

local M = {}

M.TABLE = "career_youthplayers"

local function academy()
    local t, err = db.get_table(M.TABLE)
    if not t then return nil, err end
    if not db.has_field(t, "playerid") then return nil, M.TABLE .. " has no playerid field" end
    return t
end

-- { {pid, tier, variance, rec}, .. } sorted by player id
function M.rows()
    local t, err = academy()
    if not t then return nil, err end
    local out = {}
    for rec in db.records(t) do
        local pid = t:GetRecordFieldValue(rec, "playerid")
        if pid and pid > 0 then
            out[#out + 1] = { pid = pid, rec = rec, tier = db.get(t, rec, "playertier"), variance = db.get(t, rec, "potentialvariance") }
        end
    end
    table.sort(out, function(a, b) return a.pid < b.pid end)
    return out, t
end

local function list(ctx)
    local rows, err = M.rows()
    if not rows then return false, err end
    local players = db.get_table("players")
    local prow = {}
    if players then
        for rec in db.records(players) do prow[players:GetRecordFieldValue(rec, "playerid")] = rec end
    end
    local lines = { "playerid,name,position,overall,potential,potentialvariance,playertier" }
    local parts = {}
    for _, r in ipairs(rows) do
        local rec = prow[r.pid]
        local pos = rec and db.get(players, rec, "preferredposition1") or -1
        local ovr = rec and db.get(players, rec, "overallrating") or -1
        local pot = rec and db.get(players, rec, "potential") or -1
        local name = game.player_name(r.pid)
        lines[#lines + 1] = string.format("%d,%s,%d,%d,%d,%d,%d", r.pid, '"' .. tostring(name):gsub('"', "'") .. '"', pos, ovr,
            pot, r.variance or -1, r.tier or -1)
        if #parts < 12 then parts[#parts + 1] = string.format("%s %d/%d", name, ovr, pot) end
    end
    local file = ""
    if not ctx.dry and ctx.out_dir then
        local path = util.join(ctx.out_dir, "youth_academy.csv")
        if util.write_file(path, table.concat(lines, "\n") .. "\n") then file = " -> " .. path end
    end
    if #rows == 0 then return true, "the youth academy is empty" end
    return true, string.format("%d academy players%s: %s%s", #rows, file, table.concat(parts, ", "), #rows > #parts and ", ..." or "")
end

function M.plan(cfg)
    local pid = util.to_int(cfg.playerid)
    if not pid or pid <= 0 then return nil, "playerid must be a positive whole number" end
    local rows, t = M.rows()
    if not rows then return nil, t end
    local row
    for _, r in ipairs(rows) do if r.pid == pid then row = r end end
    if not row then return nil, string.format("player %d is not in the youth academy (%s)", pid, M.TABLE) end
    local players, err = db.get_table("players")
    if not players then return nil, err end
    local prec = db.find(players, "playerid", pid)
    if not prec then return nil, string.format("player %d is not in the players table", pid) end
    local writes = {}
    local pot = util.to_int(cfg.potential) or 0
    if pot ~= 0 then
        if pot < 1 or pot > 99 then return nil, "potential must be 1..99 (0 = leave alone)" end
        writes[#writes + 1] = { players, prec, "potential", pot }
    end
    local pos = util.to_int(cfg.position)
    if pos and pos >= 0 then
        if pos > 27 then return nil, "position must be 0..27 (-1 = leave alone)" end
        writes[#writes + 1] = { players, prec, "preferredposition1", pos }
    end
    local tier = util.to_int(cfg.tier)
    if tier and tier >= 0 then writes[#writes + 1] = { t, row.rec, "playertier", tier } end
    local var = util.to_int(cfg.variance)
    if var and var >= 0 then writes[#writes + 1] = { t, row.rec, "potentialvariance", var } end
    if #writes == 0 then return nil, "nothing to change: give potential, position, tier or variance" end
    for _, w in ipairs(writes) do
        local _, verr = db.validate(w[1], w[3], w[4])
        if verr then return nil, verr end
    end
    return { pid = pid, writes = writes }
end

function M.run(ctx)
    local cfg = ctx.cfg or {}
    local mode = cfg.mode or "list"
    if mode == "list" then return list(ctx) end
    if mode ~= "set" then return false, "mode must be list or set" end
    local plan, why = M.plan(cfg)
    if not plan then return false, why end
    local parts = {}
    for _, w in ipairs(plan.writes) do
        local ok, err = db.set(w[1], w[2], w[3], w[4], ctx.dry)
        if not ok then return false, err end
        parts[#parts + 1] = string.format("%s=%d", w[3], w[4])
    end
    return true, string.format("%s%s (%d): %s", ctx.dry and "dry run: " or "", game.player_name(plan.pid), plan.pid,
        table.concat(parts, ", "))
end

return M
