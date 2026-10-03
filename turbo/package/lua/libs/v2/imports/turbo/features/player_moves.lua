-- Turbo feature: player moves (FC 26 LE v26.1.4 transfer / loan / release / terminate loan,
-- v26.2.2 transfer- and loan-list flags). Runs a list of actions in order:
--   "player_moves": { "actions": [
--     { "action": "transfer", "playerid": 158023, "to_teamid": 1, "fee": 0, "wage": 50000, "months": 36, "release_clause": -1 },
--     { "action": "loan", "playerid": 20801, "to_teamid": 241, "months": 12, "loan_to_buy": -1 },
--     { "action": "release", "playerid": 1 },
--     { "action": "terminate_loan", "playerid": 1 },
--     { "action": "transfer_list" | "loan_list" | "unlist", "scope": { "user_team": true }, "filters": { "max_overall": 70 } },
--     { "action": "delete", "playerid": 1, "confirm": true }   -- FC 26 LE v26.1.5 "Delete player"; needs confirm
--   ] }
-- All actions are validated before the first one runs.

local util = require 'imports/turbo/core/util'
local game = require 'imports/turbo/core/game'
local sel = require 'imports/turbo/core/select'

local M = {}

local NEEDS = {
    transfer = "TransferPlayer", loan = "LoanPlayer", release = "ReleasePlayerFromTeam",
    terminate_loan = "TerminateLoan", transfer_list = "AddPlayerToTransferList",
    loan_list = "AddPlayerToLoanList", unlist = "RemovePlayerFromLists",
    delete = "DeletePlayer",   -- void DeletePlayer(int playerid, int player_current_teamid = 0) (FC 27 LE DOC.MD)
}

local function player_exists(pid, player_set)
    if type(PlayerExists) == "function" then
        local ok, r = pcall(PlayerExists, pid)
        if ok and type(r) == "boolean" then return r end
    end
    return player_set[pid] == true
end

-- Validate one action. Returns normalized action or nil, error
local function check(a, i, team_set, player_set)
    if type(a) ~= "table" then return nil, string.format("action %d is not an object", i) end
    local kind = a.action
    if not NEEDS[kind] then return nil, string.format("action %d: unknown action %s", i, tostring(kind)) end
    if type(_G[NEEDS[kind]]) ~= "function" then return nil, NEEDS[kind] .. " is not available in this Live Editor build" end
    local out = { action = kind }

    if kind == "transfer_list" or kind == "loan_list" or kind == "unlist" then
        if a.playerid ~= nil then
            out.playerids = { util.to_int(a.playerid) }
            if not out.playerids[1] or not player_exists(out.playerids[1], player_set) then
                return nil, string.format("action %d: player %s not found", i, tostring(a.playerid))
            end
        else
            local list, err = sel.players(a.scope, a.filters, { allow_all = false })
            if not list then return nil, string.format("action %d: %s", i, err) end
            out.playerids = {}
            for _, p in ipairs(list) do out.playerids[#out.playerids + 1] = p.pid end
        end
        return out
    end

    out.playerid = util.to_int(a.playerid)
    if not out.playerid or not player_exists(out.playerid, player_set) then
        return nil, string.format("action %d: player %s not found", i, tostring(a.playerid))
    end
    if kind == "delete" and a.confirm ~= true then
        return nil, string.format("action %d: deleting player %d needs \"confirm\": true (it cannot be undone)", i, out.playerid)
    end
    if kind == "transfer" or kind == "loan" then
        out.to_teamid = util.to_int(a.to_teamid)
        if not out.to_teamid or not team_set[out.to_teamid] then
            return nil, string.format("action %d: team %s not found", i, tostring(a.to_teamid))
        end
        out.months = util.to_int(a.months or (kind == "loan" and 12 or 36))
        if not out.months or out.months < 1 or out.months > 120 then return nil, string.format("action %d: months must be 1..120", i) end
    end
    if kind == "transfer" then
        out.fee = util.to_int(a.fee or 0)
        out.wage = util.to_int(a.wage or 0)
        out.release_clause = util.to_int(a.release_clause or -1)
        if not out.fee or out.fee < 0 then return nil, string.format("action %d: fee must be an integer >= 0", i) end
        if not out.wage or out.wage < 0 then return nil, string.format("action %d: wage must be an integer >= 0", i) end
        if not out.release_clause or out.release_clause < -1 then return nil, string.format("action %d: release_clause must be -1 or more", i) end
    elseif kind == "loan" then
        out.loan_to_buy = util.to_int(a.loan_to_buy or -1)
        if not out.loan_to_buy then return nil, string.format("action %d: loan_to_buy must be an integer (-1 = none)", i) end
    end
    return out
end

function M.run(ctx)
    local actions = ctx.cfg.actions
    if type(actions) ~= "table" or #actions == 0 then return false, "actions list is empty" end

    local team_set, player_set = game.team_ids(), game.player_ids()
    local plan = {}
    for i, a in ipairs(actions) do
        local c, err = check(a, i, team_set, player_set)
        if not c then return false, err end
        plan[#plan + 1] = c
    end

    local done, failed = {}, 0
    local function call(fn, ...)
        if ctx.dry then return true end
        local ok = pcall(fn, ...)
        if not ok then failed = failed + 1 end
        return ok
    end
    for _, a in ipairs(plan) do
        local n = 0
        if a.action == "transfer" then
            if call(TransferPlayer, a.playerid, a.to_teamid, a.fee, a.wage, a.months, 0, a.release_clause) then n = 1 end
        elseif a.action == "loan" then
            if call(LoanPlayer, a.playerid, a.to_teamid, a.months, a.loan_to_buy, 0) then n = 1 end
        elseif a.action == "release" then
            if call(ReleasePlayerFromTeam, a.playerid) then n = 1 end
        elseif a.action == "terminate_loan" then
            if call(TerminateLoan, a.playerid) then n = 1 end
        elseif a.action == "delete" then
            if call(DeletePlayer, a.playerid, 0) then n = 1 end
        else
            local fn = _G[NEEDS[a.action]]
            for _, pid in ipairs(a.playerids) do
                if call(fn, pid, 0) then n = n + 1 end
            end
        end
        done[#done + 1] = string.format("%s x%d", a.action, n)
    end
    local summary = table.concat(done, ", ")
    if failed > 0 then summary = summary .. string.format("; %d calls failed", failed) end
    return failed == 0, summary
end

return M
