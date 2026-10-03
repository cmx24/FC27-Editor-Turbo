-- Turbo feature: current-season transfer history to CSV.
-- Port of FC 26 export_transfer_history.lua. FC 26 read the TransferManager negotiations storage at
-- +0x1DD0 and nine negotiation vectors inside it. Turbo checks that layout first and, when FC 27
-- moved the storage, searches the TransferManager for a storage whose vectors are well-formed and
-- whose entries reference real players. Nothing is exported from memory that fails these checks.
--   "export_transfer_history": {}

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local mem = require 'imports/turbo/core/mem'
local calib = require 'imports/turbo/core/calib'
local csv = require 'imports/turbo/core/csv'

local M = {}

M.STORAGE_OFF = 0x1DD0
M.VECTORS = {
    ai_club_transfers     = { off = 0x08, size = 0xB8 },
    ai_player_transfers   = { off = 0x10, size = 0xB0 },
    ai_club_loans         = { off = 0x18, size = 0xB8 },
    ai_player_loans       = { off = 0x20, size = 0x98 },
    user_club_transfers   = { off = 0x28, size = 0xA0 },
    user_club_loans       = { off = 0x30, size = 0xF8 },
    user_player_transfers = { off = 0x38, size = 0x98 },
    ai_player_exchanges   = { off = 0x40, size = 0xA8 },
    user_player_exchanges = { off = 0x48, size = 0x98 },
}
local MAX_ENTRIES = 20000

-- Iterate entries of one vector: fn(entry_addr, playerid, buying_team, selling_team)
local function each(storage, name, fn)
    local spec = M.VECTORS[name]
    local vec = mem.ptr(storage + spec.off)
    if not vec then return end
    local b, _, count = mem.vector(vec, spec.size, MAX_ENTRIES)
    if not b or count == 0 then return end
    for i = 0, count - 1 do
        local cur = b + i * spec.size
        local pid, buy, sell = mem.int(cur), mem.int(cur + 4), mem.int(cur + 8)
        if pid and buy and sell and pid > 0 and buy > 0 and sell > 0 then fn(cur, pid, buy, sell) end
    end
end

-- Validate a candidate storage. Returns score (entries referencing real players) or nil
function M.validate_storage(storage, player_set)
    if not mem.is_ptr(storage, 8) then return nil end
    local total, real = 0, 0
    for _, spec in pairs(M.VECTORS) do
        local vec = mem.ptr(storage + spec.off)
        if not vec then return nil end
        local b, _, count = mem.vector(vec, spec.size, MAX_ENTRIES)
        if not b then return nil end
        for i = 0, math.min(count, 200) - 1 do
            local pid = mem.int(b + i * spec.size)
            if pid and pid > 0 then
                total = total + 1
                if player_set[pid] then real = real + 1 end
            end
        end
    end
    if total == 0 or real < total * 0.8 then return nil end
    return real
end

-- Returns storage address, offset or nil, reason
function M.locate(player_set, hint)
    pcall(require, 'imports/career_mode/enums')
    local tm = mem.manager(ENUM_FCEGameModesFCECareerModeTransferManager)
    if not tm then return nil, "TransferManager not found" end
    local tries = {}
    local h = type(hint) == "table" and util.to_int(hint.storage_off)
    if h then tries[#tries + 1] = h end
    tries[#tries + 1] = M.STORAGE_OFF
    for _, off in ipairs(tries) do
        local s = mem.ptr(tm + off)
        if s and M.validate_storage(s, player_set) then return s, off end
    end
    for off = 0x400, 0x4000, 8 do
        local s = mem.ptr(tm + off)
        if s and M.validate_storage(s, player_set) then return s, off end
    end
    return nil, "no negotiations storage with well-formed vectors and real players was found (needs at least one transfer this season)"
end

local function last_action(cur, begin_off)
    local ab = mem.ptr(cur + begin_off, 4)
    local ae = mem.ptr(cur + begin_off + 8, 4)
    if not ab or not ae or ab == ae or ae < ab then return nil end
    return mem.byte(ae - 0xC + 0x8), mem.int(ae - 0xC)
end

function M.collect(storage)
    local player_negos, club_negos = {}, {}
    local function key(prefix, pid, buy, sell) return string.format("%s%d-%d-%d", prefix, pid, buy, sell) end

    -- player-side negotiations (who moved, when)
    each(storage, "ai_player_transfers", function(cur, pid, buy, sell)
        if mem.bool(cur + 0x67) then
            local idx = mem.byte(cur + 0x6C) or 0
            player_negos[key("T", pid, buy, sell)] = { playerid = pid, buying_team = buy, selling_team = sell,
                date = mem.int(cur + 0x70 + 0xC * idx), type = "transfer" }
        end
    end)
    each(storage, "ai_player_exchanges", function(cur, pid, buy, sell)
        if mem.bool(cur + 0x67) then
            local idx = mem.byte(cur + 0x6B) or 1
            player_negos[key("T", pid, buy, sell)] = { playerid = pid, buying_team = buy, selling_team = sell,
                date = mem.int(cur + 0x6C + 0xC * (idx - 1)), type = "transfer" }
        end
    end)
    each(storage, "ai_player_loans", function(cur, pid, buy, sell)
        if mem.bool(cur + 0x52) then
            local idx = mem.byte(cur + 0x57) or 1
            player_negos[key("L", pid, buy, sell)] = { playerid = pid, buying_team = buy, selling_team = sell,
                date = mem.int(cur + 0x58 + 0xC * (idx - 1)), type = "loan" }
        end
    end)
    for _, name in ipairs({ "user_player_transfers", "user_player_exchanges" }) do
        each(storage, name, function(cur, pid, buy, sell)
            local act, date = last_action(cur, 0x50)
            if act == 0 or act == 4 then
                player_negos[key("T", pid, buy, sell)] = { playerid = pid, buying_team = buy, selling_team = sell,
                    date = date, type = "transfer" }
            end
        end)
    end

    -- club-side negotiations (fees)
    each(storage, "ai_club_transfers", function(cur, pid, buy, sell)
        local seller, buyer = mem.bool(cur + 0x6E), mem.bool(cur + 0x6F)
        if seller or buyer then
            local fee, exch
            exch = 0
            if seller then
                local p = mem.ptr(cur + 0x28, 4)
                fee = p and mem.int(p - 0xC) or 0
            else
                local p = mem.ptr(cur + 0x48, 4)
                fee = p and mem.int(p - 0x14) or 0
                exch = p and mem.int(p - 0x14 + 0x4) or 0
            end
            club_negos[key("T", pid, buy, sell)] = { final_fee = fee, exchange_value = exch }
        end
    end)
    each(storage, "user_club_transfers", function(cur, pid, buy, sell)
        local act = last_action(cur, 0x58)
        if act == 0 or act == 4 then
            local p = mem.ptr(cur + (act == 0 and 0x20 or 0x40), 4)
            if p then
                club_negos[key("T", pid, buy, sell)] = {
                    exchange_player = mem.int(p - 0x28) or 0,
                    exchange_value = mem.int(p - 0x28 + 0x4) or 0,
                    final_fee = mem.int(p - 0x28 + 0xC) or 0,
                }
            end
        end
    end)
    each(storage, "ai_club_loans", function(cur, pid, buy, sell)
        if mem.bool(cur + 0x72) or mem.bool(cur + 0x73) then club_negos[key("L", pid, buy, sell)] = { final_fee = 0 } end
    end)
    each(storage, "user_club_loans", function(cur, pid, buy, sell)
        local act = last_action(cur, 0x50)
        if act == 0 or act == 4 then club_negos[key("L", pid, buy, sell)] = { final_fee = 0 } end
    end)
    return player_negos, club_negos
end

local COLUMNS = { "type", "date", "playerid", "playername", "teamfromid", "teamfromname", "teamtoid", "teamtoname",
    "fee", "exchangeplayerid", "exchangeplayername", "total_deal_value" }

function M.run(ctx)
    if not ctx.out_dir then return false, "no writable output folder" end
    local player_set = game.player_ids()
    if util.count(player_set) == 0 then return false, "players table not readable" end

    if not mem.map_available() then return false, mem.NO_MAP end
    local storage, off_or_err = M.locate(player_set, calib.get(ctx.out_dir, "transfer_history"))
    if not storage then return false, off_or_err end
    calib.put(ctx.out_dir, "transfer_history", { storage_off = off_or_err })

    local player_negos, club_negos = M.collect(storage)
    local rows = {}
    for k, p in pairs(player_negos) do
        local c = club_negos[k] or {}
        local exch = (c.exchange_player and c.exchange_player > 0) and c.exchange_player or nil
        rows[#rows + 1] = {
            type = p.type, date = p.date, playerid = p.playerid, playername = game.player_name(p.playerid),
            teamfromid = p.selling_team, teamfromname = game.team_name(p.selling_team),
            teamtoid = p.buying_team, teamtoname = game.team_name(p.buying_team),
            fee = c.final_fee or 0,
            exchangeplayerid = exch or "", exchangeplayername = exch and game.player_name(exch) or "",
            total_deal_value = (c.final_fee or 0) + (c.exchange_value or 0),
        }
    end
    table.sort(rows, function(a, b)
        if (a.date or 0) ~= (b.date or 0) then return (a.date or 0) < (b.date or 0) end
        return a.playerid < b.playerid
    end)

    local path = util.join(ctx.out_dir, "turbo_transfer_history_" .. util.timestamp_suffix(game.current_date()) .. ".csv")
    if not ctx.dry then
        local wok, werr = csv.write(path, COLUMNS, rows)
        if not wok then return false, "cannot write " .. path .. ": " .. tostring(werr) end
    end
    return true, string.format("%d moves saved to %s (storage at TransferManager+0x%X)", #rows, path, off_or_err)
end

return M
